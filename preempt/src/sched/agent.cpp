#include "xsched/types.h"
#include "xsched/utils/log.h"
#include "xsched/utils/str.h"
#include "xsched/utils/xassert.h"
#include "xsched/protocol/def.h"
#include "xsched/protocol/names.h"
#include "xsched/preempt/sched/agent.h"
#include "xsched/preempt/sched/executor.h"
#include "xsched/sched/scheduler/local.h"
#include "xsched/xsched.h"

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>


using namespace xsched::sched;
using namespace xsched::protocol;

namespace xsched::preempt
{

#define XSCHED_CTL_FILE "/tmp/xsched-policy"

// ---------------------------------------------------------------------------
// Scheduler mutex — protects scheduler_ against concurrent access from the
// socket listener thread (ReplaceScheduler) and the main thread (FiniScheduler).
// ---------------------------------------------------------------------------
static std::mutex g_sched_mutex;

// ---------------------------------------------------------------------------
// Command executor (shared by SIGUSR1 handler and Unix socket listener)
// ---------------------------------------------------------------------------

static std::atomic<XPolicyType> g_last_policy{kPolicyUnknown};
static std::atomic<Timeslice>   g_last_ts{0};
static std::atomic<size_t>      g_last_kd{1};

// Self-pipe for signal → thread handoff (async-signal-safe signal handling)
static int g_sig_pipe_r = -1;
static int g_sig_pipe_w = -1;

static std::atomic<bool> g_shutdown{false};

static std::string xsched_execute_line(const char *key, const char *val)
{
    if (std::strcmp(key, "POLICY") == 0) {
        if (std::strcmp(val, "?") == 0 || std::strcmp(val, "help") == 0) {
            std::string help;
            help += "POLICY=HPF|HHPF|UP|PUP|KEDF|LAX|AWF|CHPF\n";
            help += "TIMESLICE=100..100000   (us, for UP/PUP)\n";
            help += "KDEADLINE=1..N          (concurrency, for KEDF)\n";
            help += "STATUS                   show current config\n";
            help += "HELP                     this message\n";
            help += "example: POLICY=KEDF KDEADLINE=3";
            return help;
        }
        XPolicyType type = GetPolicyType(val);
        if (type == kPolicyUnknown)
            return std::string("ERR: unknown policy '") + val + "'";
        XResult ret = SchedAgent::SetScheduler(kSchedulerLocal, type);
        if (ret == kXSchedSuccess) g_last_policy = type;
        XINFO("xsched: policy -> %s", val);
        std::string resp;
        if (ret == kXSchedSuccess) {
            resp = std::string("OK: policy switched to ") + val;
            // hint about required parameters
            if (type == kPolicyUtilizationPartition || type == kPolicyProcessUtilizationPartition)
                resp += "\nhint: use TIMESLICE=<us> to set timeslice (current: " +
                        (g_last_ts > 0 ? std::to_string(g_last_ts) : "0") + " us)";
            else if (type == kPolicyKEarliestDeadlineFirst)
                resp += "\nhint: use KDEADLINE=<N> to set concurrency (current: " +
                        std::to_string(g_last_kd) + ")";
        } else {
            resp = "ERR: policy switch failed";
        }
        return resp;
    }

    if (std::strcmp(key, "TIMESLICE") == 0) {
        long ts = std::atol(val);
        if (ts < 100 || ts > 100000)
            return std::string("ERR: TIMESLICE out of range [100, 100000]: ") + val;
        XResult ret = XHintTimeslice((Timeslice)ts);
        if (ret == kXSchedSuccess) g_last_ts = (Timeslice)ts;
        XINFO("xsched: TIMESLICE -> %ld us", ts);
        return (ret == kXSchedSuccess)
            ? std::string("OK: TIMESLICE set to ") + val + " us"
            : "ERR: TIMESLICE set failed";
    }

    if (std::strcmp(key, "KDEADLINE") == 0) {
        long k = std::atol(val);
        if (k < 1)
            return std::string("ERR: KDEADLINE must be >= 1: ") + val;
        XResult ret = XHintKDeadline((size_t)k);
        if (ret == kXSchedSuccess) g_last_kd = (size_t)k;
        XINFO("xsched: KDEADLINE -> %ld", k);
        return (ret == kXSchedSuccess)
            ? std::string("OK: KDEADLINE set to ") + val
            : "ERR: KDEADLINE set failed";
    }

    if (std::strcmp(key, "STATUS") == 0) {
        std::string s;
        const char *names[] = {
            "HPF", "HHPF", "UP", "PUP", "KEDF", "LAX", "AWF", "CHPF", "UNKNOWN"
        };
        int idx = (g_last_policy >= kPolicyHighestPriorityFirst &&
                   g_last_policy <= kPolicyCPUHighestPriorityFirst)
                  ? (int)g_last_policy - 1 : 8;
        s += "POLICY="   + std::string(names[idx]);
        s += " TIMESLICE=" + (g_last_ts > 0 ? std::to_string(g_last_ts) : "0");
        s += " KDEADLINE=" + std::to_string(g_last_kd);
        return s;
    }

    return std::string("ERR: unknown command '") + key + "'";
}

// ---------------------------------------------------------------------------
// Config-file processor (called from the socket thread on SIGUSR1)
// ---------------------------------------------------------------------------

static void xsched_process_config_file()
{
    int fd = ::open(XSCHED_CTL_FILE, O_RDONLY);
    if (fd < 0) {
        XWARN("xsched: cannot open %s (%s)", XSCHED_CTL_FILE, ::strerror(errno));
        return;
    }

    // Only accept files owned by the current user
    struct stat file_stat;
    if (::fstat(fd, &file_stat) == 0 && file_stat.st_uid != ::getuid()) {
        XWARN("xsched: %s owned by uid %d, ignoring", XSCHED_CTL_FILE, (int)file_stat.st_uid);
        ::close(fd);
        return;
    }

    char buf[512];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);
    if (n <= 0) {
        XWARN("xsched: empty config file");
        return;
    }
    buf[n] = '\0';

    char *line = buf;
    char *saveptr = nullptr;
    while ((line = strtok_r(line, "\n\r", &saveptr)) != nullptr) {
        if (line[0] == '\0' || line[0] == '#') { line = nullptr; continue; }
        char *eq = std::strchr(line, '=');
        if (!eq) { line = nullptr; continue; }
        *eq = '\0';
        char *key = line, *val = eq + 1;
        line = nullptr;
        while (*key == ' ' || *key == '\t') key++;
        while (*val == ' ' || *val == '\t') val++;
        xsched_execute_line(key, val);
    }
}

// ---------------------------------------------------------------------------
// SIGUSR1 handler — async-signal-safe: only writes one byte to self-pipe
// ---------------------------------------------------------------------------

static void xsched_sigusr1_handler(int sig) {
    (void)sig;
    char byte = 1;
    // write() is listed as async-signal-safe by POSIX
    ssize_t r = ::write(g_sig_pipe_w, &byte, 1);
    (void)r;
}

// ---------------------------------------------------------------------------
// Unix domain socket listener (primary runtime interface)
// ---------------------------------------------------------------------------

// Linux abstract namespace socket (sun_path[0] = '\0') — no filesystem entry,
// immune to TOCTOU / symlink attacks.  Automatically cleaned up on exit.
static std::string xsched_socket_name()
{
    return std::string("xsched-") + std::to_string(getpid());
}

static void xsched_socket_listener()
{
    const std::string name = xsched_socket_name();

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        XWARN("xsched: failed to create socket (%s)", ::strerror(errno));
        return;
    }

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    // Abstract namespace: sun_path[0] = '\0', name starts at offset 1
    std::strncpy(addr.sun_path + 1, name.c_str(), sizeof(addr.sun_path) - 2);

    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + 1 + name.size();
    if (::bind(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        XWARN("xsched: bind(@%s) failed (%s)", name.c_str(), ::strerror(errno));
        ::close(fd);
        return;
    }

    if (::listen(fd, 5) < 0) {
        XWARN("xsched: listen failed (%s)", ::strerror(errno));
        ::close(fd);
        return;
    }

    XINFO("xsched: listening on @%s", name.c_str());

    // Poll both the listening socket and the self-pipe for signal delivery
    struct pollfd fds[2];
    fds[0].fd     = fd;
    fds[0].events = POLLIN;
    fds[1].fd     = g_sig_pipe_r;
    fds[1].events = POLLIN;

    while (!g_shutdown.load()) {
        int ret = ::poll(fds, 2, 500);  // 500ms timeout so shutdown check is responsive
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }

        // Handle client connections
        if (fds[0].revents & POLLIN) {
            int cli = ::accept(fd, nullptr, nullptr);
            if (cli < 0) {
                XWARN("xsched: accept failed (%s)", ::strerror(errno));
                continue;
            }

            // SO_PEERCRED: verify the connecting process belongs to the same user
            struct ucred peer;
            socklen_t peer_len = sizeof(peer);
            if (::getsockopt(cli, SOL_SOCKET, SO_PEERCRED, &peer, &peer_len) == 0 &&
                peer.uid != ::getuid()) {
                XWARN("xsched: connection rejected from uid %d", (int)peer.uid);
                ::close(cli);
                continue;
            }

            char buf[4096];
            ssize_t n = ::read(cli, buf, sizeof(buf) - 1);
            if (n < 0) {
                XWARN("xsched: read error (%s)", ::strerror(errno));
                ::close(cli);
                continue;
            }
            if (n == 0) {
                ::close(cli);
                continue;
            }

            buf[n] = '\0';
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
                buf[--n] = '\0';

            char *eq = std::strchr(buf, '=');
            std::string resp;
            if (eq) {
                *eq = '\0';
                char *k = buf, *v = eq + 1;
                while (*k == ' ' || *k == '\t') k++;
                while (*v == ' ' || *v == '\t') v++;
                resp = xsched_execute_line(k, v);
            } else {
                resp = xsched_execute_line(buf, "");
            }
            resp += '\n';

            // Ensure the entire response is written (handle partial write)
            const char *wp = resp.c_str();
            size_t remain = resp.size();
            while (remain > 0) {
                ssize_t written = ::write(cli, wp, remain);
                if (written <= 0) break;
                wp += written;
                remain -= written;
            }

            ::close(cli);
        }

        // Handle SIGUSR1 — process the config file in thread context
        if (fds[1].revents & POLLIN) {
            char byte;
            while (::read(g_sig_pipe_r, &byte, 1) > 0) {}
            xsched_process_config_file();
        }
    }

    ::close(fd);
}

SchedAgent SchedAgent::g_sched_agent;

SchedAgent::SchedAgent()
{
    if (scheduler_ != nullptr) return;

    XSchedulerType scheduler_type = kSchedulerUnknown;
    XPolicyType policy_type = kPolicyUnknown;
    char *scheduler_str = std::getenv(XSCHED_SCHEDULER_ENV_NAME);
    char *policy_str = std::getenv(XSCHED_POLICY_ENV_NAME);
    if (scheduler_str) scheduler_type = GetSchedulerType(scheduler_str);
    if (policy_str) policy_type = GetPolicyType(policy_str);

    SchedExecutor::Start();
    InitScheduler(scheduler_type, policy_type);

    // Sync global state from the actual scheduler configuration
    if (policy_type != kPolicyUnknown) g_last_policy = policy_type;

    // One-time IPC initialization — flag reset in destructor for rebuild support
    static bool pipe_initialized = false;
    if (!pipe_initialized) {
        pipe_initialized = true;
        int pipe_fds[2];
        if (::pipe(pipe_fds) == 0) {
            g_sig_pipe_r = pipe_fds[0];
            g_sig_pipe_w = pipe_fds[1];
            int flags = ::fcntl(g_sig_pipe_r, F_GETFL, 0);
            ::fcntl(g_sig_pipe_r, F_SETFL, flags | O_NONBLOCK);
        } else {
            XWARN("xsched: failed to create self-pipe (%s)", ::strerror(errno));
        }

        // Register SIGUSR1 handler (only if pipe exists)
        if (g_sig_pipe_r >= 0) {
            struct sigaction sa;
            sa.sa_handler = xsched_sigusr1_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_RESTART;
            if (sigaction(SIGUSR1, &sa, nullptr) == 0) {
                XINFO("xsched: policy switch via kill -USR1 %d (%s)", getpid(), XSCHED_CTL_FILE);
            } else {
                XWARN("xsched: failed to register SIGUSR1 handler (%s)", strerror(errno));
            }
        }
    }

    // Start Unix domain socket listener for change-xsched-policy tool
    try {
        std::thread(xsched_socket_listener).detach();
    } catch (...) {
        XWARN("xsched: failed to start socket listener thread");
    }
}

SchedAgent::~SchedAgent()
{
    if (scheduler_ == nullptr) return;

    // Signal the socket listener thread to shut down
    g_shutdown.store(true);
    if (g_sig_pipe_w != -1) {
        char byte = 1;
        ::write(g_sig_pipe_w, &byte, 1);  // wake up poll()
    }

    // Destroy the scheduler under the mutex to prevent races with ReplaceScheduler
    {
        std::lock_guard<std::mutex> lock(g_sched_mutex);
        SchedExecutor::Stop();
        FiniScheduler();
    }
    // Note: g_shutdown remains true — do NOT reset it.  The detached socket
    // thread checks this flag and will exit its poll loop.  Resetting it
    // would allow the thread to continue running after scheduler_ is gone.
    // Pipe fds are process-lifetime resources; the kernel closes them on exit.
    // Static state (g_last_*) is left as-is — caller should not rely on it
    // after destruction.
}

void SchedAgent::RelayHint(std::shared_ptr<const sched::Hint> hint)
{
    if (scheduler_ == nullptr) {
        XWARN("scheduler not initialized, hint type(%d) dropped", hint->Type());
        return;
    }
    scheduler_->RecvEvent(std::make_shared<HintEvent>(hint));
}

void SchedAgent::RelayEvent(std::shared_ptr<const Event> event)
{
    if (scheduler_ == nullptr) {
        XWARN("scheduler not initialized, event type(%d) dropped", event->Type());
        return;
    }
    scheduler_->RecvEvent(event);
}

void SchedAgent::InitScheduler(XSchedulerType scheduler_type, XPolicyType policy_type)
{
    if (scheduler_ != nullptr) return;
    
    auto tmp_scheduler = CreateScheduler(scheduler_type, policy_type);
    tmp_scheduler->SetExecutor(SchedExecutor::Execute);
    tmp_scheduler->Run();

    static const std::string kCmdline = []() -> std::string {
        std::string cmdline;
        
#if defined(_WIN32)
        LPWSTR cmdlineW = GetCommandLineW();
        int size = WideCharToMultiByte(CP_UTF8, 0, cmdlineW, -1, NULL, 0, NULL, NULL);
        if (size > 0) {
            std::vector<char> buffer(size);
            WideCharToMultiByte(CP_UTF8, 0, cmdlineW, -1, buffer.data(), size, NULL, NULL);
            cmdline = buffer.data();
        }
#elif defined(__linux__)
        std::ifstream cmdline_file("/proc/self/cmdline");
        if (cmdline_file.good()) {
            std::string arg;
            while (std::getline(cmdline_file, arg, '\0') && !arg.empty()) {
                cmdline = cmdline + arg + " ";
            }
        }
        cmdline_file.close();
#endif

        return ShrinkString(cmdline, ProcessCreateEvent::CmdlineCapacity() - 1);
    }();

    auto event = std::make_shared<ProcessCreateEvent>(kCmdline);
    tmp_scheduler->RecvEvent(event);

    scheduler_ = tmp_scheduler;
}

void SchedAgent::FiniScheduler()
{
    if (scheduler_ == nullptr) return;
    auto tmp_scheduler = scheduler_;
    scheduler_ = nullptr;

    auto event = std::make_shared<ProcessDestroyEvent>();
    tmp_scheduler->RecvEvent(event);
    tmp_scheduler->Stop();
}

XResult SchedAgent::ReplaceScheduler(XSchedulerType scheduler_type, XPolicyType policy_type)
{
    std::lock_guard<std::mutex> lock(g_sched_mutex);

    if (scheduler_type <= kSchedulerUnknown || scheduler_type >= kSchedulerMax) {
        XWARN("invalid scheduler type: %d", scheduler_type);
        return kXSchedErrorInvalidValue;
    }

    if (scheduler_ == nullptr) {
        XWARN("scheduler not initialized, replace scheduler failed");
        return kXSchedErrorUnknown;
    }

    if (scheduler_->GetType() == scheduler_type) {
        if (scheduler_type != kSchedulerLocal) return kXSchedSuccess; // no need to replace
        if (policy_type <= kPolicyUnknown || policy_type >= kPolicyMax) {
            XWARN("invalid policy type: %d", policy_type);
            return kXSchedErrorInvalidValue;
        }
        // only replace the policy
        auto lcl_scheduler = std::dynamic_pointer_cast<LocalScheduler>(scheduler_);
        XASSERT(lcl_scheduler != nullptr, "scheduler is not a local scheduler");
        lcl_scheduler->SetPolicy(policy_type);
        return kXSchedSuccess;
    }

    // should replace the scheduler
    FiniScheduler();
    InitScheduler(scheduler_type, policy_type);
    return kXSchedSuccess;
}

} // namespace xsched::preempt
