---
title: "processkit typed API"
summary: "Source-derived public signatures, properties, parameters, defaults, and return values."
category: "Reference"
order: 21
types: ["PipeProcess", "ProcessException", "ProcessInspectionLinuxProcessInfo", "ProcessInspectionWindowsNativeFileTime", "ProcessInspectionWindowsProcessIO", "ProcessInspectionWindowsProcessMemory", "ProcessInspectionWindowsProcessSnapshot", "ProcessUsageSnapshot", "RunningProcessInfo"]
dependencies: []
symbols: ["PipeProcess", "ProcessException", "ProcessInspectionLinuxProcessInfo", "ProcessInspectionWindowsNativeFileTime", "ProcessInspectionWindowsProcessIO", "ProcessInspectionWindowsProcessMemory", "ProcessInspectionWindowsProcessSnapshot", "ProcessUsageSnapshot", "RunningProcessInfo"]
related: ["api.md", "getting-started.md", "index.md"]
---

# processkit typed API

Generated from the package source declarations. Platform guards are shown where present; inherited members are documented on the base type. See the [API contracts](api.md) for behavior and limitations.

## RunningProcessInfo

`Public Class RunningProcessInfo`

### Properties

| Name | Type | Read-Only | Shared | Default | Description |
| --- | --- | --- | --- | --- | --- |
| `ProcessId` | `Integer` | Yes | No | — | — |
| `ParentProcessId` | `Optional Of Integer` | Yes | No | — | — |
| `Name` | `String` | Yes | No | — | — |
| `ExecutablePath` | `Optional Of String` | Yes | No | — | — |
| `ThreadCount` | `Optional Of Integer` | Yes | No | — | — |
| `StartTime` | `Optional Of DateTime` | Yes | No | — | — |

### Constructor

`Public Sub Constructor(pProcessId As Integer, pParentProcessId As Optional Of Integer, pName As String, pExecutablePath As Optional Of String, pThreadCount As Optional Of Integer, pStartTime As Optional Of DateTime)`

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `pProcessId` | `Integer` | `ByVal` | Required |
| `pParentProcessId` | `Optional Of Integer` | `ByVal` | Required |
| `pName` | `String` | `ByVal` | Required |
| `pExecutablePath` | `Optional Of String` | `ByVal` | Required |
| `pThreadCount` | `Optional Of Integer` | `ByVal` | Required |
| `pStartTime` | `Optional Of DateTime` | `ByVal` | Required |

Returns: no value.

## ProcessUsageSnapshot

`Public Class ProcessUsageSnapshot`

### Properties

| Name | Type | Read-Only | Shared | Default | Description |
| --- | --- | --- | --- | --- | --- |
| `KernelSeconds` | `Double` | Yes | No | — | — |
| `UserSeconds` | `Double` | Yes | No | — | — |
| `ResidentBytes` | `Optional Of Integer` | Yes | No | — | — |
| `HandleCount` | `Optional Of Integer` | Yes | No | — | — |

### Constructor

`Public Sub Constructor(pKernelSeconds As Double, pUserSeconds As Double, pResidentBytes As Optional Of Integer, pHandleCount As Optional Of Integer)`

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `pKernelSeconds` | `Double` | `ByVal` | Required |
| `pUserSeconds` | `Double` | `ByVal` | Required |
| `pResidentBytes` | `Optional Of Integer` | `ByVal` | Required |
| `pHandleCount` | `Optional Of Integer` | `ByVal` | Required |

Returns: no value.

## ProcessInspection functions

### ProcessInspectionSupported

`Public Function ProcessInspectionSupported() As Boolean`

Read-only inspection is implemented for Linux/glibc and Windows x64/ARM64.

No parameters.

#### Returns

| Type |
| --- |
| `Boolean` |

### CurrentProcessId

`Public Function CurrentProcessId() As Integer`

No parameters.

#### Returns

| Type |
| --- |
| `Integer` |

### RunningProcessIds

`Public Function RunningProcessIds() As Array Of Integer`

Owned PID snapshot. PIDs can exit or be reused immediately; this grants no ownership.

No parameters.

#### Returns

| Type |
| --- |
| `Array Of Integer` |

### ReadRunningProcess

`Public Function ReadRunningProcess(processId As Integer) As Optional Of RunningProcessInfo`

None means absent/exited; denied optional fields stay None. Snapshots are not atomic.

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `processId` | `Integer` | `ByVal` | Required |

#### Returns

| Type |
| --- |
| `Optional Of RunningProcessInfo` |

### ReadCurrentProcessUsage

`Public Function ReadCurrentProcessUsage() As ProcessUsageSnapshot`

CPU seconds and current resident bytes, not peak RSS. Fields are sampled separately.

No parameters.

#### Returns

| Type |
| --- |
| `ProcessUsageSnapshot` |

## ProcessInspectionLinuxProcessInfo

`Public Class ProcessInspectionLinuxProcessInfo`

### Properties

| Name | Type | Read-Only | Shared | Default | Description |
| --- | --- | --- | --- | --- | --- |
| `ProcessId` | `Integer` | No | No | — | — |
| `Name` | `String` | No | No | — | — |
| `State` | `String` | No | No | — | — |
| `ParentProcessId` | `Integer` | No | No | — | — |
| `ThreadCount` | `Integer` | No | No | — | — |
| `StartTimeTicks` | `Integer` | No | No | — | — |
| `ExecutablePath` | `Optional Of String` | No | No | — | — |
| `ExecutableError` | `Integer` | No | No | — | — |
| `UserTicks` | `Integer` | No | No | — | — |
| `KernelTicks` | `Integer` | No | No | — | — |
| `ResidentPages` | `Integer` | No | No | — | — |

## ProcessInspectionLinuxBackend functions

### ProcessInspectionLinuxIsSupported

`Public Function ProcessInspectionLinuxIsSupported() As Boolean`

No parameters.

#### Returns

| Type |
| --- |
| `Boolean` |

### ProcessInspectionLinuxParseStat

`Public Function ProcessInspectionLinuxParseStat(text As String) As ProcessInspectionLinuxProcessInfo`

Parse through stat field 22; comm may contain spaces, parentheses and newlines.

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `text` | `String` | `ByVal` | Required |

#### Returns

| Type |
| --- |
| `ProcessInspectionLinuxProcessInfo` |

### ProcessInspectionLinuxProcessIds

`Public Function ProcessInspectionLinuxProcessIds() As Array Of Integer`

Unsorted PID snapshot from this mount/PID namespace; entries can disappear immediately.

No parameters.

#### Returns

| Type |
| --- |
| `Array Of Integer` |

### ProcessInspectionLinuxReadProcess

`Public Function ProcessInspectionLinuxReadProcess(pid As Integer) As ProcessInspectionLinuxProcessInfo`

Read stat then exe. ExecutableError preserves readlink errno without losing stat data.

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `pid` | `Integer` | `ByVal` | Required |

#### Returns

| Type |
| --- |
| `ProcessInspectionLinuxProcessInfo` |

## ProcessInspectionWindowsProcessMemory

`Sequential Structure ProcessInspectionWindowsProcessMemory`

### Fields

| Name | Type | Value / Default | Description |
| --- | --- | --- | --- |
| `Size` | `UInt32` | — | — |
| `PageFaultCount` | `UInt32` | — | — |
| `PeakWorkingSetBytes` | `UInt64` | — | — |
| `WorkingSetBytes` | `UInt64` | — | — |
| `PeakPagedPoolBytes` | `UInt64` | — | — |
| `PagedPoolBytes` | `UInt64` | — | — |
| `PeakNonPagedPoolBytes` | `UInt64` | — | — |
| `NonPagedPoolBytes` | `UInt64` | — | — |
| `CommitBytes` | `UInt64` | — | — |
| `PeakCommitBytes` | `UInt64` | — | — |
| `PrivateCommitBytes` | `UInt64` | — | — |

## ProcessInspectionWindowsProcessIO

`Sequential Structure ProcessInspectionWindowsProcessIO`

### Fields

| Name | Type | Value / Default | Description |
| --- | --- | --- | --- |
| `ReadOperations` | `UInt64` | — | — |
| `WriteOperations` | `UInt64` | — | — |
| `OtherOperations` | `UInt64` | — | — |
| `ReadBytes` | `UInt64` | — | — |
| `WriteBytes` | `UInt64` | — | — |
| `OtherBytes` | `UInt64` | — | — |

## ProcessInspectionWindowsNativeFileTime

`Sequential Structure ProcessInspectionWindowsNativeFileTime`

### Fields

| Name | Type | Value / Default | Description |
| --- | --- | --- | --- |
| `Low` | `UInt32` | — | — |
| `High` | `UInt32` | — | — |

## ProcessInspectionWindowsProcessSnapshot

`Public Class ProcessInspectionWindowsProcessSnapshot`

### Properties

| Name | Type | Read-Only | Shared | Default | Description |
| --- | --- | --- | --- | --- | --- |
| `CreationFileTime` | `UInt64` | No | No | — | — |
| `KernelSeconds` | `Double` | No | No | — | — |
| `UserSeconds` | `Double` | No | No | — | — |
| `HandleCount` | `Integer` | No | No | — | — |
| `Memory` | `ProcessInspectionWindowsProcessMemory` | No | No | — | — |
| `IO` | `ProcessInspectionWindowsProcessIO` | No | No | — | — |

## ProcessInspectionWindowsBackend functions

### ProcessInspectionWindowsIsSupported

`Public Function ProcessInspectionWindowsIsSupported() As Boolean`

No parameters.

#### Returns

| Type |
| --- |
| `Boolean` |

### ProcessInspectionWindowsReadCurrentProcess

`Public Function ProcessInspectionWindowsReadCurrentProcess() As ProcessInspectionWindowsProcessSnapshot`

Sample CPU, memory, handles and IO for this process; fields are not atomic together.

No parameters.

#### Returns

| Type |
| --- |
| `ProcessInspectionWindowsProcessSnapshot` |

### ProcessInspectionWindowsCreationTime

`Public Function ProcessInspectionWindowsCreationTime(process As Ptr) As Optional Of DateTime`

Availability: `#If TargetWindows Then`

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `process` | `Ptr` | `ByVal` | Required |

#### Returns

| Type |
| --- |
| `Optional Of DateTime` |

## ProcessException

`Public Class ProcessException Inherits Exception`

### Constructor

`Public Sub Constructor(message As String)`

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `message` | `String` | `ByVal` | Required |

Returns: no value.

## PipeProcess

`Public Class PipeProcess`

Shell-free executable with separate stdin, stdout, and stderr pipes. Confine to one thread.

### Properties

| Name | Type | Read-Only | Shared | Default | Description |
| --- | --- | --- | --- | --- | --- |
| `IsRunning` | `Boolean` | Yes | No | — | — |
| `ExitCode` | `Integer` | Yes | No | — | — |
| `OutputClosed` | `Boolean` | Yes | No | — | — |
| `ErrorClosed` | `Boolean` | Yes | No | — | — |
| `TerminationSignal` | `Integer` | Yes | No | — | — |

### Constructor

`Public Sub Constructor(executablePath As String, arguments As Array Of String, workingDirectory As String = "", ownTree As Boolean = False)`

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `executablePath` | `String` | `ByVal` | Required |
| `arguments` | `Array Of String` | `ByVal` | Required |
| `workingDirectory` | `String` | `ByVal` | `""` |
| `ownTree` | `Boolean` | `ByVal` | `False` |

Returns: no value.

### Constructor (overload 2)

`Public Sub Constructor(executablePath As String, arguments As Array Of String, workingDirectory As String, environmentOverrides As Map Of String, String, ownTree As Boolean = False)`

Inherit the parent's environment and override only these names for this child.

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `executablePath` | `String` | `ByVal` | Required |
| `arguments` | `Array Of String` | `ByVal` | Required |
| `workingDirectory` | `String` | `ByVal` | Required |
| `environmentOverrides` | `Map Of String, String` | `ByVal` | Required |
| `ownTree` | `Boolean` | `ByVal` | `False` |

Returns: no value.

### Poll

`Public Function Poll() As Boolean`

True while running; False once exited. Poll also reaps POSIX children.

No parameters.

#### Returns

| Type |
| --- |
| `Boolean` |

### ReadOutput

`Public Function ReadOutput(maxCount As Integer = 65536) As Bytes`

Empty bytes means no data now; OutputClosed distinguishes observed EOF.

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `maxCount` | `Integer` | `ByVal` | `65536` |

#### Returns

| Type |
| --- |
| `Bytes` |

### ReadError

`Public Function ReadError(maxCount As Integer = 65536) As Bytes`

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `maxCount` | `Integer` | `ByVal` | `65536` |

#### Returns

| Type |
| --- |
| `Bytes` |

### WriteInput

`Public Function WriteInput(data As Bytes, offset As Integer = 0) As Integer`

Returns bytes accepted, possibly zero. Retry the remainder after draining output.

#### Parameters

| Name | Type | Passing | Default |
| --- | --- | --- | --- |
| `data` | `Bytes` | `ByVal` | Required |
| `offset` | `Integer` | `ByVal` | `0` |

#### Returns

| Type |
| --- |
| `Integer` |

### CloseInput

`Public Sub CloseInput()`

No parameters.

Returns: no value.

### Terminate

`Public Sub Terminate()`

Force-terminates the direct child; Poll observes its completion.

No parameters.

Returns: no value.

### TerminateTree

`Public Sub TerminateTree()`

Kill the opted-in process group/job, including descendants after parent exit.

No parameters.

Returns: no value.

### Close

`Public Sub Close()`

Idempotent. Terminates and waits for a still-running direct child, then frees pipes.

No parameters.

Returns: no value.

### Destructor

`Public Sub Destructor()`

No parameters.

Returns: no value.
