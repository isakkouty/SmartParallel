# SmartParallel v1.7 profile access policies

Profile access controls file-backed evidence behavior independently from execution mode.

## Policies

| Policy | Construction/load | Operation behavior | Explicit save |
|---|---|---|---|
| `Disabled` | No profile file is loaded. | Adaptive uses in-process evidence. Deterministic generic callbacks require an explicit forced scheduler. | Not available as a file-backed workflow. |
| `ReadOnly` | Loads and validates a database. | Adaptive may warm-start; Deterministic may replay exact Approved entries. | Operations never save and the source file remains unchanged. |
| `ReadWrite` | Loads when a path is supplied and permits mutable in-memory Candidate evidence. | Adaptive may learn and update the in-memory database. | Allowed only through explicit `save_profiles` or tools. |

## I/O boundary

Profile-file reads occur only during Runtime construction or explicit `load_profiles`. Writes occur only during explicit `save_profiles` or tool commands. Telemetry includes counters that verify operations performed no profile-file reads or writes.

There is no destructor save, periodic save, operation-time save, or implicit approval.

## Recommended deployment pattern

- Calibration environment: Adaptive + ReadWrite.
- Review and approval: command-line tools on immutable Candidate evidence.
- Production replay: Deterministic + ReadOnly.
- Existing applications without persistence: Adaptive + Disabled.

Concurrent multi-process writers are unsupported. Use one designated writer and distribute Approved files as immutable artifacts.
