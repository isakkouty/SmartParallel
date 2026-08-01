# SmartParallel v1.7 Adaptive and Deterministic execution

Execution mode controls whether plans may be learned or changed. It is independent from the numerical policy that controls floating-point evaluation.

## Mode comparison

| Behavior | Adaptive | Deterministic |
|---|---|---|
| In-memory learning | Allowed | Forbidden |
| Timing/holdout/drift probes | Allowed | Forbidden |
| Route switching | Allowed | Forbidden |
| Compatible loaded Candidate | Warm-start evidence | Rejected |
| Compatible loaded Approved | Warm-start evidence | Exact replay source |
| Missing/incompatible semantic profile | Learn current context | Fail closed |
| Profile-file mutation by operation | Never | Never |
| Automatic fallback | Normal adaptive behavior | No fallback |

## Adaptive execution

Adaptive execution may learn, reuse in-memory evidence, authenticate one loaded restart warm start, monitor current behavior, and replace stale decisions. A loaded profile is a starting point rather than a permanent freeze.

ReadOnly mode keeps the file unchanged. ReadWrite mode permits explicit export of updated Candidate evidence; operations still do not write files.

## Deterministic execution

Deterministic semantic operations require an exact Approved profile. The Runtime validates compatibility and replays the saved route, scheduler, worker budget, numerical plan, provider identity, and capability requirements.

Missing, Candidate, corrupted, expired, unavailable, or incompatible profiles fail before destination modification. There is no silent scheduler substitution and no fallback to Adaptive mode.

Generic callbacks cannot be assigned a persistent semantic identity automatically. With profile access Disabled, they may execute Deterministically only through an explicit forced scheduler.

## Telemetry invariant

A valid Deterministic replay records deterministic calls while keeping learning samples, timing probes, holdout probes, drift probes, route switches, and profile mutations at zero. The release cross-process pilot verifies this invariant in two fresh processes.
