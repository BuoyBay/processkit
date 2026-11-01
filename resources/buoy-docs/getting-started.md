---
title: "ProcessKit getting started"
summary: "Install ProcessKit and try a complete usage example."
category: "Guides"
order: 10
keywords: ["process", "subprocess", "pipes", "stdin", "stdout", "stderr", "environment"]
related: ["index.md", "api.md", "typed-api.md"]
---

# Getting started

Copy `processkit-0.1.0.buipkg`, with its matching native target slice, into `packages/`. Import `processkit`.

## Run an explicitly chosen executable

Pass an absolute executable path as the first application argument, followed by literal child arguments. The example bounds captured bytes and polling attempts, retains split UTF-8 until complete output, and always releases the process. It may throw on non-text output; keep Bytes instead when consuming binary output.

```buoy
Import processkit

Sub Main(args As Array Of String)
    If Array.Count(args) < 2 Then Throw New InvalidArgumentException("Pass an absolute executable path")
    Dim arguments As Array Of String = []
    For i As Integer = 2 To Array.Count(args) - 1
        Array.Append(arguments, args(i))
    Next
    Dim process As New PipeProcess(args(1), arguments)
    Try
        process.CloseInput()
        Dim output As New Bytes()
        Dim errors As New Bytes()
        Dim polls As Integer = 0
        While True
            Dim chunk As Bytes = process.ReadOutput()
            Dim errorChunk As Bytes = process.ReadError()
            If output.Count + chunk.Count > 1048576 Or errors.Count + errorChunk.Count > 1048576 Then
                Throw New InvalidArgumentException("Captured output limit reached")
            End If
            output.Append(chunk)
            errors.Append(errorChunk)
            Dim running As Boolean = process.Poll()
            If Not running And process.OutputClosed And process.ErrorClosed Then Exit While
            polls += 1
            If polls > 10000 Then Throw New InvalidArgumentException("Polling limit reached")
            Thread.Sleep(1)
        End While
        Print output.ToString(Text.Encodings.UTF8)
        Print errors.ToString(Text.Encodings.UTF8)
        Print "Exit: " + String.From(process.ExitCode)
    Finally
        process.Close()
    End Try
End Sub
```

## Integrate with an IDE

A UI should poll from its timer/task system, use a real operation deadline, and render incremental output through a streaming decoder such as BuildKit. The example's poll count is a simple bound, not a wall-clock timeout.

`WriteInput` can accept fewer bytes than requested, including zero. Retry the remainder while draining both output streams. An empty read can mean no data yet or observed EOF; use the closed flags. A process can exit with unread output still buffered. Closing stdin sends EOF and is repeatable while the process remains open.

OS failures throw `ProcessException`; invalid arguments and use after Close throw `InvalidArgumentException`. Close can wait for forced termination, so it is not an asynchronous UI action. Validate original untrusted bytes before String conversion if embedded-NUL rejection matters, because the Buoy runtime can truncate those strings before this API sees them.

## Cancel and finish an operation

Choose ownership before launch. If cancelling a build must also stop its child
processes, construct `PipeProcess` with `ownTree = True`. The default owns only
the direct child; calling `Terminate()` does not terminate its descendants.
`TerminateTree()` requires that launch-time opt-in and can clean up owned
descendants after the direct child has exited. A POSIX child that starts a new
session can escape the owned group.

Keep one owner responsible for polling, cancellation and cleanup. On a cancel
request, stop submitting input and call `Terminate()` or `TerminateTree()` for
the ownership policy chosen at launch. These request forced termination, not a
graceful application shutdown. `CloseInput()` only sends stdin EOF; whether that
ends the child is up to the program.

Continue draining both output streams and polling if final diagnostics matter.
Wait for `Poll()` to return False before reading `ExitCode`, and keep reading
until both `OutputClosed` and `ErrorClosed` are True. An empty read alone is not
EOF, and direct-child exit does not prove that descendants have closed inherited
pipes. Apply an operation deadline and output byte limits to this drain phase
as well. Capture the exit status and any output you need before `Close()`.

Always put `Close()` in cleanup that runs on success, cancellation and exceptions,
as the example does with `Finally`. Closing releases the pipes and can discard
unread output; it also terminates and waits for a live direct child and cleans up
an opted-in tree. Repeated Close is safe, but further process operations are
invalid. Closing can block during termination, so account for that in UI shutdown
rather than treating it as an asynchronous cancellation notification.

[Package overview](index.md) · [API reference](api.md)
