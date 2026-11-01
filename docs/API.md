# ProcessKit · 0.1.0

Shell-free subprocess pipes for IDE build tasks, test runners and language-server
clients. Unlike Buoy's `Shell`, this package passes an argument array directly to
a native executable and keeps stdout and stderr separate. It never invokes a
command interpreter as a fallback.

## Build and use

```sh
python3 tools/build_processkit.py
```

Copy `packages/processkit-0.1.0.buipkg` into a consumer project's `packages/`, then
`Import processkit`. The builder requires Buoy 0.72+, Python 3.9+ and a C11
compiler. It has no downloaded source or third-party runtime dependencies.
macOS uses Clang, Linux uses `cc`, and Windows supports a matching Visual Studio
compiler or MinGW. A Windows x64 cross build is reproducible with:

```sh
python3 tools/build_processkit.py --target x86_64-windows --cc x86_64-w64-mingw32-gcc --native-only
```

The Windows ARM64 slice is cross-built with Zig 0.14.1, the same toolchain as
compressionkit's ARM64 DLL. Delete the `.pdb` that Zig writes beside the DLL; it is not shipped:

```sh
python3 tools/build_processkit.py --target arm64-windows --native-only \
  --cc "$ZIG cc -target aarch64-windows-gnu"
```

An MSVC ARM64 build (builder run inside `vcvarsall x64_arm64` with `--cc cl`)
should also work, but it is untested: no ARM64 MSVC toolset was available.

`--native-only` builds the selected slice; `--seal-only` checks all six Buoy
targets and seals source-matched slices present in `runtime/` and `link/`. `BUOY` selects the
Buoy compiler. macOS and Linux libraries double as link libraries; Windows has
a separate import `.lib`. Collect target directories before sealing a package
for multiple targets. Generated native directories are ignored by Git.

See [the runnable example](../examples/processkit_demo.bui). Its first argument
is an executable path; all remaining arguments are passed literally.

| API | Behavior |
| --- | --- |
| `PipeProcess(executablePath, arguments, workingDirectory = "", ownTree = False)` | Start an executable with exact arguments and inherited environment; opt into group/job ownership |
| `PipeProcess(executablePath, arguments, workingDirectory, environmentOverrides, ownTree = False)` | Inherit the environment, then override names from a `Map Of String, String` for this child only |
| `WriteInput(bytes, offset = 0)` | Return bytes accepted; zero means backpressure; retry the remainder |
| `CloseInput()` | Send EOF by closing stdin; repeatable while the process object is open |
| `ReadOutput(maxCount = 65536)` | Drain up to the byte limit from stdout without waiting |
| `ReadError(maxCount = 65536)` | Drain up to the byte limit from stderr without waiting |
| `Poll()` / `IsRunning` | True while running; polls/reaps the direct child |
| `ExitCode` | Exit status after completion; throws while still running |
| `OutputClosed` / `ErrorClosed` | True after a read observes EOF on the corresponding stream |
| `Terminate()` | Force-terminate the direct child; continue polling to observe completion |
| `TerminateTree()` | Force-terminate an opted-in owned process group/job, including descendants after the direct child exits |
| `TerminationSignal` | POSIX signal after termination, otherwise zero; `ExitCode` keeps its existing 128+signal convention |
| `Close()` | Idempotently terminate/reap an unfinished child, kill an owned tree and release all pipes |

The executable path must identify a real executable. Use an absolute path for
portable behavior; no PATH search is provided. An empty working directory inherits
the parent's directory. Arguments may contain spaces, quotes, metacharacters,
Unicode and empty strings; embedded NUL bytes reject. POSIX uses `execve`, so an
executable text file without a valid shebang rejects instead of running a shell.
Windows uses `CreateProcessW`, UTF-16 paths and standard Microsoft CRT argument
quoting; arbitrary programs with custom command-line parsers may interpret their
command line differently. Windows command-line construction has a conservative
32K UTF-16 bound. Batch files require an explicitly selected command interpreter.

Environment overrides never call a process-wide environment setter and never
change the parent environment. Each spawn receives its own inherited snapshot
plus overrides. Names must be nonempty and contain no `=`; values may be empty. Both use
Buoy's NUL-terminated UTF-8 `String` representation. Buoy 0.72 truncates embedded
NUL when converting bytes to a string, before this API can validate it; validate
original byte input before conversion if strict NUL rejection is required. There are at most 4,096 overrides. POSIX
names are case-sensitive; Windows names are case-insensitive and the child gets
a sorted Unicode environment block. A Windows map containing case variants of
the same name should be avoided because the last enumerated variant wins. An
empty value sets the variable to the empty string; this API does not delete
inherited variables. Do not put credentials into argv: environment overrides
provide a child-only handoff without command-line exposure. Owned merged environment blocks and stored override values are cleared on
release, and failures never include override values.

Reads return `Bytes` so split UTF-8 sequences and binary data remain intact.
An empty read means either no data yet or EOF; the closed flags distinguish
observed EOF. A process can exit while its pipes still contain unread data.
Drain both streams until EOF to retain trailing output. Read limits are
1..1,048,576 bytes. Writes accept at most 1 MiB per call and may accept fewer
bytes; drain output while retrying writes so a child that writes before reading
cannot deadlock the caller. Writes to closed child stdin throw `ProcessException`
without delivering SIGPIPE to the parent on POSIX.

OS failures throw `ProcessException`; invalid arguments or use after `Close`
throw `InvalidArgumentException`. ARC calls `Close` automatically. Keep each
process object on one thread or synchronize access externally. Close can wait
while the OS completes forced termination; it is not an asynchronous UI action.

## Scope and verification

This is a pipe transport, not a terminal emulator or PTY. It exposes no shell
expansion or interactive job control. By default, Terminate and Close affect the
direct child only. With `ownTree: True`, POSIX creates a supervised session/process group;
Windows creates a kill-on-close job and assigns the suspended child before
resuming it. Failure to obtain ownership fails the spawn. `TerminateTree()` and
`Close()` then kill owned descendants too, even after the parent exits. Calling
TerminateTree without ownership throws. The POSIX session leader monitors a
private owner pipe and kills its group if the owning process dies, including
after a suspended owner is force-killed. It stays alive until handle cleanup so
the group identifier cannot be reused before cleanup. Command status and output
remain separate from the supervisor, and `Terminate()` still stops the command
alone.

Owned POSIX spawns require a waitable `SIGCHLD` disposition: `SIG_IGN` and
`SA_NOCLDWAIT` reject before spawning rather than losing exit status. A custom
external child reaper must not reap the owned supervisor. Poll observes a killed
supervisor without reaping it until Close; repeated tree termination is
idempotent, and an invalidated identity is never used to signal a reused group.

Descendants can retain output pipes until
cleanup, so callers must still enforce operation deadlines. A POSIX descendant
that deliberately creates a separate session can escape the group; this is not
a security sandbox. The POSIX
child closes inherited descriptors other than stdin/stdout/stderr, including
ones above limits that were lowered after opening them. Its fallback computes
a bound in the parent using the hard limit and `/proc/self/fd` or `/dev/fd`;
these standard descriptor directories must be available. Windows uses
an explicit inherited-handle allowlist.

The Linux native and sealed Buoy tests cover inherited/overridden Unicode and
empty environment values, child isolation without parent mutation, invalid
environment names, exact Unicode/metacharacter/empty
arguments, binary stdin and output, separated stderr, fragmented reads,
backpressure, missing executables, working directories, non-inherited descriptors,
no fallback shell, broken stdin, exit codes, explicit close and child reaping.
The Windows x64 native bridge compiles with strict MinGW warnings. All module,
test and example sources are compiler-checked on six Buoy targets. Actual native
execution is verified only on platforms recorded in the repository's combined
developer-tools verification report.

The new owned-tree API is runtime-verified on Linux x86_64; its Windows x86_64
bridge cross-compiles. Earlier direct-child APIs were also runtime-verified on
macOS arm64. The ownership extension still needs macOS/Windows runtime checks.
Rebuild each target slice from the current C source. The builder writes a
`.source-sha256` receipt beside each native artifact; sealing includes only
matching slices and their import libraries, leaving old artifacts untouched.
Collect those receipts along with native slices before multi-target sealing.
The generic developer-tool builder uses the same receipt-aware sealer.


## Read-only process inspection

`Import processkit` also exposes `CurrentProcessId()`, `RunningProcessIds()`, `ReadRunningProcess(processId)` and `ReadCurrentProcessUsage()`. Check `ProcessInspectionSupported()` before inspection: Linux/glibc and Windows x64/ARM64 are implemented; macOS currently exposes only the current PID and reports inspection unsupported.

`RunningProcessInfo` and `ProcessUsageSnapshot` are owned read-only snapshots. Enumeration does not grant process ownership or termination rights. IDs can exit or be reused immediately; snapshots are not atomic across calls. `ReadRunningProcess` returns `None` for an absent/exited PID; optional executable paths, creation times and other fields remain `None` when unavailable or denied. Failure to enumerate or read the essential Linux stat record remains an exception. Linux accepts positive Int32 PIDs; Windows accepts 1..4294967295 and omits the special idle PID zero. Invalid PID bounds throw.

CPU fields are cumulative user/kernel **seconds**, not utilization percentages. `ResidentBytes` reports current RSS/working set, not peak memory; overflow is rejected. Linux uses `/proc` in the caller’s PID/mount namespace and glibc clock/page units; `StartTime` and `HandleCount` are currently `None`. Windows uses Toolhelp32 snapshots, limited-query process handles, native creation time in UTC, and current working-set/handle counters. Handles are closed internally; no handles escape.

Maintainers run the private validation suite before release. See the repository README for public build instructions.


Validation receipt: inspection-native.json. The Windows owned-child fixture uses the OS PowerShell executable; the Linux fixture uses a Buoy child. The first Windows Buoy-child fixture timed out waiting for its flushed output; this observation has not been reduced or attributed to the inspection API. No native Windows Buoy-child stdio acceptance is claimed. The decoder naming workaround encountered during implementation is recorded separately as draft BUI-019, pending independent review.


## Linux ARM64 bridge

The native slice set now includes Linux ARM64 (glibc), alongside Linux x64,
macOS ARM64/x64, Windows x64 and Windows ARM64 (see below). Rebuild Linux ARM64
on an ARM64 Linux host with:

```sh
python3 tools/build_processkit.py --target arm64-linux --cc 'gcc -D_FORTIFY_SOURCE=3' --native-only
```

The post-fork startup-error reporter preserves errno, retries interrupted writes,
advances short writes, and exits on zero or failed writes. It uses only
async-signal-safe operations and preserves the scalar ABI. Strict warning flags
and fortification remain enabled.

ARM64 bridge validation records the native
Ubuntu 24.04/GCC 13.3 build, existing native pipe and sealed `PipeProcess` tests,
startup-error fault injection, and owned-child inspection. All five source-matched
slices are retained by the existing filtered sealer. Validation sealed only in a
private temporary staging root. After independent approval, the parent copied
the exact reviewed five-slice archive to `packages/` and `dist/`; the publication
receipt is `validation/arm64-bridge-published.json`. See the handoff for exact
hashes, retained bytes, remaining gaps, and independent-review status.

## Windows ARM64 slice

`runtime/aarch64-pc-windows-msvc/buoy_processkit.dll` and its import library
`link/aarch64-pc-windows-msvc/buoy_processkit.lib` are cross-built from the
current `bridge.c` with Zig `aarch64-windows-gnu` (command above) and sealed
with the other five slices. The build is warning-free under `-Wall -Wextra -Werror`.
The DLL exports the same 19 `bpk_*` symbols as the x64 slice. It imports only
`KERNEL32.dll` and the in-box UCRT `api-ms-win-crt-*` API sets. Consumers such as
`tests/processkit_test.bui` and Studio's `StudioLanguageServer.bui` now link for
`--target arm64-windows`.

The slice is **not yet executed on Windows ARM64**: no ARM64 Windows host was
available. Pipe, owned-tree and inspection behaviour there is unverified. See
Windows ARM64 slice validation.
