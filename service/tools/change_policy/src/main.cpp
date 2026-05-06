/*
 * change-xsched-policy — Runtime XSched Policy Switcher
 *
 * Connects to a running XSched-enabled process via Unix domain socket
 * and sends scheduling commands (policy switch, timeslice, etc.).
 *
 * Usage:
 *   change-xsched-policy help
 *   change-xsched-policy policy <NAME>
 *   change-xsched-policy timeslice <USEC>
 *   change-xsched-policy kdeadline <N>
 *   change-xsched-policy status
 *   change-xsched-policy -p <PID> policy KEDF
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <string>
#include <vector>
#include <dirent.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fstream>

static void die(const char *msg) {
    std::fprintf(stderr, "Error: %s\n", msg);
    std::exit(1);
}

// ---------------------------------------------------------------------------
// Find PIDs of processes that have libpreempt.so loaded
// ---------------------------------------------------------------------------
static std::vector<int> find_xsched_pids() {
    std::vector<int> pids;
    DIR *proc = ::opendir("/proc");
    if (!proc) return pids;

    struct dirent *entry;
    while ((entry = ::readdir(proc)) != nullptr) {
        // Only numeric directory names (PIDs)
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;

        std::string path = std::string("/proc/") + entry->d_name + "/maps";
        std::ifstream maps(path);
        if (!maps.is_open()) continue;

        std::string line;
        bool found = false;
        while (std::getline(maps, line)) {
            if (line.find("libpreempt") != std::string::npos) {
                found = true;
                break;
            }
        }
        if (found) pids.push_back(std::atoi(entry->d_name));
    }
    ::closedir(proc);
    return pids;
}

// ---------------------------------------------------------------------------
// Connect to the socket of a target process and send a command
// ---------------------------------------------------------------------------
static std::string send_command(int pid, const std::string &cmd) {
    std::string sock_name = "xsched-" + std::to_string(pid);

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return "ERR: cannot create socket";

    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    // Abstract namespace: sun_path[0] = '\0', name starts at offset 1
    std::strncpy(addr.sun_path + 1, sock_name.c_str(), sizeof(addr.sun_path) - 2);

    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + 1 + sock_name.size();
    if (::connect(fd, (struct sockaddr *)&addr, addr_len) < 0) {
        ::close(fd);
        return "ERR: cannot connect to PID " + std::to_string(pid) +
               " (@xsched-" + std::to_string(pid) + ")";
    }

    // Ensure the entire command is sent (handle partial write)
    const char *wp = cmd.c_str();
    size_t remain = cmd.size();
    while (remain > 0) {
        ssize_t written = ::write(fd, wp, remain);
        if (written <= 0) break;
        wp += written;
        remain -= written;
    }

    char buf[4096];
    ssize_t n = ::read(fd, buf, sizeof(buf) - 1);
    ::close(fd);

    if (n <= 0) return "ERR: no response";
    buf[n] = '\0';
    // trim trailing newline
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
    return buf;
}

// ---------------------------------------------------------------------------
// Help
// ---------------------------------------------------------------------------
static void usage(const char *prog) {
    const char *name = std::strrchr(prog, '/');
    name = name ? name + 1 : prog;

    std::printf(
        "Usage: %s [options] <command> [args]\n"
        "\n"
        "Commands:\n"
        "  help                          Show this help message.\n"
        "  policy <NAME>                 Switch scheduling policy (runtime).\n"
        "                                  HPF, HHPF, UP, PUP, KEDF, LAX, AWF, CHPF\n"
        "  timeslice <USEC>              Set timeslice in microseconds.\n"
        "                                  Range: 100 .. 100000  (for UP/PUP)\n"
        "  kdeadline <N>                 Set K deadline concurrency.\n"
        "                                  Range: >= 1  (for KEDF)\n"
        "  status                        Show current scheduling config.\n"
        "\n"
        "Options:\n"
        "  -p <PID>   Target a specific process ID.\n"
        "             Default: auto-detect from /proc/*/maps\n"
        "\n"
        "Environment:\n"
        "  XSCHED_PID   Target a specific process ID (same as -p).\n"
        "\n"
        "Examples:\n"
        "  %s help\n"
        "  %s policy KEDF\n"
        "  %s -p 12345 kdeadline 3\n"
        "  %s status\n",
        name, name, name, name, name
    );
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
    int target_pid = 0;

    // Parse -p <PID> option
    int optind = 1;
    if (argc > 2 && std::strcmp(argv[1], "-p") == 0) {
        target_pid = std::atoi(argv[2]);
        if (target_pid <= 0) die("invalid PID");
        optind = 3;
    }

    // Command
    if (optind >= argc) {
        usage(argv[0]);
        return 0;
    }

    std::string cmd_name = argv[optind];
    std::string cmd_arg  = (optind + 1 < argc) ? argv[optind + 1] : "";

    // Build the socket command string
    std::string sock_cmd;
    if (cmd_name == "help") {
        usage(argv[0]);
        return 0;
    } else if (cmd_name == "status") {
        sock_cmd = "STATUS";
    } else if (cmd_name == "policy") {
        if (cmd_arg.empty()) die("missing policy name");
        sock_cmd = "POLICY=" + cmd_arg;
    } else if (cmd_name == "timeslice") {
        if (cmd_arg.empty()) die("missing timeslice value");
        sock_cmd = "TIMESLICE=" + cmd_arg;
    } else if (cmd_name == "kdeadline") {
        if (cmd_arg.empty()) die("missing K deadline value");
        sock_cmd = "KDEADLINE=" + cmd_arg;
    } else {
        std::fprintf(stderr, "Error: unknown command '%s'\n", cmd_name.c_str());
        usage(argv[0]);
        return 1;
    }

    // Determine target PID(s)
    std::vector<int> pids;
    if (target_pid > 0) {
        pids.push_back(target_pid);
    } else {
        const char *env_pid = std::getenv("XSCHED_PID");
        if (env_pid && env_pid[0] != '\0') {
            pids.push_back(std::atoi(env_pid));
        } else {
            pids = find_xsched_pids();
            if (pids.empty())
                die("no XSched-enabled process found; use -p <PID> or XSCHED_PID=<pid>");
        }
    }

    // Send command to each target
    bool any_ok = false;
    for (int pid : pids) {
        std::string resp = send_command(pid, sock_cmd);
        if (resp.compare(0, 3, "OK:") == 0) {
            std::printf("PID %d: %s\n", pid, resp.c_str());
            any_ok = true;
        } else {
            std::fprintf(stderr, "PID %d: %s\n", pid, resp.c_str());
        }
    }

    return any_ok ? 0 : 1;
}
