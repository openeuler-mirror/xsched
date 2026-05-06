# Change Scheduling Policy at Runtime

This example demonstrates how to switch XSched scheduling policies at runtime
without restarting the application.

## Basic Idea

Normally, the scheduling policy is set at startup via the `XSCHED_POLICY`
environment variable and cannot be changed without restarting the process.

With the Unix domain socket listener built into `libpreempt.so`, every
XSched-enabled process listens on an abstract namespace socket
(`@xsched-<pid>`) at runtime — no filesystem entry, no TOCTOU/symlink
attacks.
The `change-xsched-policy` tool connects to this socket and sends scheduling
commands.  The policy switch is handled by `SchedAgent::SetScheduler()`, which
performs a lightweight on-line switch: it stops the worker thread, destroys and
recreates the policy object, resumes all XQueues, and continues execution.

## Build XSched

Make sure you have already built XSched with CUDA support.  The
`change-xsched-policy` tool is built automatically as part of this step.

```bash
# go to the root directory of XSched
cd xsched
# by default, XSched will be installed to xsched/output
make cuda INSTALL_PATH=<install_path>
```

The tool binary will be installed to `change-xsched-policy`.

## Usage

```bash
# Show help
change-xsched-policy help

# Switch to KEDF with concurrency 3
change-xsched-policy policy KEDF
change-xsched-policy kdeadline 3

# Switch to UP with 5 ms timeslice
change-xsched-policy policy UP
change-xsched-policy timeslice 5000

# Show current configuration
change-xsched-policy status

# Target a specific process by PID
change-xsched-policy -p 12345 policy HPF
```

### Available Policies

| Policy | Name | Parameters |
|--------|------|-----------|
| `HPF` | Highest Priority First | (none) |
| `HHPF` | Heterogeneous Highest Priority First | (none) |
| `UP` | Utilization Partition | `timeslice <us>` (100..100000) |
| `PUP` | Process Utilization Partition | `timeslice <us>` (100..100000) |
| `KEDF` | K Earliest Deadline First | `kdeadline <N>` (>= 1) |
| `LAX` | Laxity | (none) |
| `AWF` | Active Window First | (none) |
| `CHPF` | CPU Highest Priority First | (none) |

## Run the Example

**Step 1**: Open a terminal and start an XSched-enabled application. For
example, the transparent scheduling app with the LOCAL scheduler:

```bash
export XSCHED_SCHEDULER=LOCAL
export XSCHED_POLICY=HPF
export LD_LIBRARY_PATH=<install_path>/lib:$LD_LIBRARY_PATH

# run the app
./app
```

**Step 2**: Open another terminal and switch the scheduling policy at runtime:

```bash
change-xsched-policy policy UP
change-xsched-policy timeslice 10000
```

You will see the policy change confirmed in the app's log output:

```
policy changed from HPF to UP
```

**Step 3**: Verify the current configuration:

```bash
change-xsched-policy status
```

Expected output:

```
PID 12345: POLICY=UP TIMESLICE=10000 KDEADLINE=1
```

## Auto-Detection

By default, the tool scans `/proc/*/maps` for all processes that have
`libpreempt.so` loaded, and commands are sent to each of them.
To target a specific process:

```bash
# Via -p flag
change-xsched-policy -p 12345 policy HPF

# Via environment variable
XSCHED_PID=12345 change-xsched-policy status
```

