---
title: "ProcessKit"
summary: "Launch exact subprocess arguments with separate bounded pipe operations."
category: "Overview"
order: 0
keywords: ["process", "subprocess", "pipes", "stdin", "stdout", "stderr", "environment"]
related: ["getting-started.md", "api.md", "typed-api.md"]
---

# ProcessKit

ProcessKit is a native pipe transport for IDE build tasks, test runners, and language-server clients. It launches an executable with an explicit argument array, without shell expansion or a command-interpreter fallback. Stdout and stderr remain separate, and reads/writes preserve raw bytes.

The package includes a small C bridge. A consumer needs the runtime slice for its target; Windows also needs a matching import library. Use absolute executable paths: this API does not search PATH. The optional working directory defaults to the parent's directory, and the child inherits the environment unless child-only overrides are supplied.

## Lifecycle

Keep one owner for each process object and confine it to one thread unless synchronized. Drain both streams while writing, close stdin when input is complete, poll/reap the direct child, and continue draining until both streams report EOF. Exit and pipe closure are separate events.

`Close()` is idempotent and releases pipes, terminating/reaping an unfinished direct child. ARC also closes the object. Default termination affects only the direct child. Opt-in group/job ownership adds descendant cleanup; it is not a security sandbox.

## Read next

- [Getting started](getting-started.md)
- [API reference](api.md)
