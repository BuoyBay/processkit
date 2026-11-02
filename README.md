# Child processes with pipes

Shell-free subprocess pipes for IDE build tasks, test runners and language-server
clients. Unlike the built-in `Shell`, this package passes an argument array directly to
a native executable and keeps stdout and stderr separate. It never invokes a
command interpreter as a fallback.

## Install

From a Buoy consumer project with BPM installed:

```sh
bpm add processkit --git https://github.com/BuoyBay/processkit.git --rev refs/heads/main
```

BPM records the resolved commit in its lockfile. Keep that lockfile with the
consumer project for reproducible installs.

## Example

```basic
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

## Build and API reference

Source is in `modules/`, with native/runtime inputs where required. Use the
[package guide](docs/API.md) for backend-specific build prerequisites, ownership
rules and supported targets. Rebuilding a native backend requires that target's
compiler and SDK; sealing an archive alone does not rebuild the backend.

## Local Buoy setup

On your own machine, install Buoy and run:

```sh
buoy ai init --both
```

## Contributing

[Contributing](CONTRIBUTING.md) · [License](LICENSE)
