---
title: "ProcessKit API"
summary: "Public types and operations supplied by ProcessKit."
category: "Reference"
order: 20
keywords: ["process", "subprocess", "pipes", "stdin", "stdout", "stderr", "environment"]
symbols: ["PipeProcess", "ProcessException", "PipeProcess.Poll", "PipeProcess.ReadOutput", "PipeProcess.ReadError", "PipeProcess.WriteInput", "PipeProcess.CloseInput", "PipeProcess.Terminate", "PipeProcess.TerminateTree", "PipeProcess.Close", "PipeProcess.ExitCode", "PipeProcess.OutputClosed", "PipeProcess.ErrorClosed"]
related: ["index.md", "getting-started.md", "typed-api.md"]
---

# API reference

## Launch

| Symbol | Contract |
| --- | --- |
| `PipeProcess(executablePath, arguments, workingDirectory = "", ownTree = False)` | Start an exact argv executable with inherited environment. |
| `PipeProcess(executablePath, arguments, workingDirectory, environmentOverrides, ownTree = False)` | Override inherited variables for this child only, from a Map of String to String. |
| `ProcessException` | Native allocation, spawn, pipe, or OS failure. |

Environment names must be nonempty without equals signs; empty values set empty strings, not deletion. There are at most 4096 overrides. POSIX names are case-sensitive; Windows names are case-insensitive, so avoid case variants. Overrides never mutate the parent environment. Windows uses standard Microsoft CRT argv quoting and a conservative 32K UTF-16 command-line bound; programs with custom parsers may behave differently.

## Pipe and state operations

| Symbol | Contract |
| --- | --- |
| `ReadOutput(maxCount = 65536)`, `ReadError(maxCount = 65536)` | Nonblocking raw reads; maxCount must be 1 through 1048576. |
| `WriteInput(data, offset = 0)` | Return accepted byte count, possibly zero; each call accepts at most 1 MiB. |
| `CloseInput()` | Send stdin EOF. |
| `Poll()`, `IsRunning` | Check/reap direct child; True while running. |
| `ExitCode` | Available only after completion; reading while running throws. |
| `OutputClosed`, `ErrorClosed` | Whether a read observed stream EOF. |
| `TerminationSignal` | POSIX termination signal or zero; ExitCode retains 128-plus-signal convention. |

## Cleanup and platform scope

`Terminate()` force-terminates the direct child. `TerminateTree()` requires `ownTree = True`; POSIX uses a new session/process group and Windows a kill-on-close job. Ownership failure rejects spawn. Owned descendants can be killed even after the direct child exits; a POSIX descendant creating a separate session can escape. `Close()` also cleans up an owned tree and releases resources; repeating Close is safe.

This is not a PTY or terminal emulator. POSIX text executables require a valid shebang; Windows batch files require an explicitly selected command interpreter. The ownership extension has Linux runtime coverage and Windows cross-compilation; macOS/Windows runtime validation remains target-specific work. Rebuild native slices from matching bridge source before distributing them.

[Getting started](getting-started.md) · [Package overview](index.md)


[Full typed API: properties, parameters, defaults, and returns](typed-api.md)
