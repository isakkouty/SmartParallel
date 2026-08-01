# SmartParallel v1.7 benchmark methodology

The v1.7 publication separates correctness, execution authentication, reproducibility, and performance. A fast result is never accepted merely because it completed, and a noisy performance interval is never treated as a proven regression.

## Questions measured

The suite asks:

1. Does the existing free-function API retain comparable latency to explicit Runtime and copied-context calls?
2. Does loading compatible evidence provide a measurable Adaptive restart warm start?
3. Does Deterministic replay remain within the declared latency envelope of warm Adaptive execution?
4. Does profile parsing scale acceptably through 1,000 exact entries?
5. Do calibration and compatibility checks produce authenticated evidence?
6. Do two fresh processes reproduce the same stable experiment identity?
7. Do all retained v1.6 correctness, reproducibility, numerical, and performance-sanity gates still pass?

## Paired startup evidence

Every measured startup repetition creates three fresh Runtime instances:

- **Adaptive cold:** ReadWrite access, no loaded profile, and new Candidate evidence;
- **Adaptive warm:** a compatible loaded Candidate profile that must authenticate as restart evidence;
- **Deterministic:** a compatible loaded Approved profile that must replay without adaptive maintenance.

The variants receive identical inputs and must produce identical output. Their order rotates by repetition to avoid a fixed first/last bias. A separate unmeasured operation warms the shared backend without warming any measured Runtime.

The calibration Runtime is never reused as a source of “cold” timing. Repeatedly timing one Runtime would mix first-use dispatch with progressively warmer process-local state and would not represent a restart comparison.

## API-overhead evidence

The unchanged free function, explicit Runtime call, and copied `ExecutionContext` call execute the same AXPY workload. Repetition-matched differences are analyzed rather than comparing unrelated aggregate medians.

## Correctness and authentication

Outside timed regions, the benchmark verifies:

- complete output equality for cold, warm, and Deterministic startup cases;
- fresh Runtime construction for every startup sample;
- authenticated warm-start state for loaded Adaptive evidence;
- Approved profile state for Deterministic execution;
- zero learning samples, timing probes, and profile mutations during Deterministic replay;
- valid profile integrity and exact compatibility outcomes;
- expected rejection of incompatible identities.

Cross-process identity and v1.6 regression status are validated by separate release-script stages.

## Statistical acceptance

The analyzer uses repetition-matched pairs and deterministic bootstrap 95% intervals:

| Objective | Acceptance rule |
|---|---|
| Explicit Runtime overhead | Absolute paired overhead ≤20 µs. |
| Copied-context overhead | Absolute paired overhead ≤20 µs. |
| Adaptive warm start | At least 1.5× faster than fresh cold Adaptive execution. |
| Deterministic latency | No more than 25% slower than warm Adaptive execution. |
| 1,000-entry profile load | Upper 95% bound below 1,000 ms. |

A complete interval satisfying an objective is `PASS`. A complete interval violating it is `FAIL`. An interval crossing the boundary is `INCONCLUSIVE-PASS`: the point estimate is reported, but the data does not establish a statistically credible failure.

The profile-scale guard is a construction-time bound of approximately 1 ms per entry at 1,000 entries. Profile files are never read on operation hot paths.

## Smoke versus full publication

`smoke` mode validates schemas, correctness, authentication, evidence generation, consumers, CLI replay, source packaging, and exact-ZIP rebuilding with a short sample count. It does not apply publication-grade statistical performance gates to underpowered samples.

`full` mode applies the strict benchmark objectives and extended compiler/dependency matrices. The accepted Windows publication used `31 full`.

## Retained v1.6 guard

The complete v1.6 scientific suite remains a separate regression guard. It validates numerical accuracy, execution validity, route authentication, cross-scheduler reproducibility, fixed pointwise plans, full-output digests, Fast compatibility, and broad scientific-kernel performance sanity.

All results are machine-specific. The release claim concerns validated behavior and honest evidence classification, not universal speed.
