# Execution leases

An `ExecutionLease` is a move-only admission token. It records the requested and granted workers, nesting mode, governor association, and stable lease fingerprint inputs.

A root lease owns permits. An inherited child references the parent state but owns no additional process permits. Public callers may inherit a bounded view of a parent lease; concurrent partition accounting is intentionally not exposed as a public v1.8 API.

Lease destruction is `noexcept`, releases root permits exactly once, and remains safe during exception unwinding.
