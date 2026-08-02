# SmartParallel v1.7.0 — Reproducible Runtime release notes

**Trust statement:** **v1.7 — Trust the experiment.**

v1.7 adds explicit Runtime ownership and an auditable path from adaptive calibration to exact approved replay. It preserves all validated v1.0–v1.6 execution and numerical behavior while making configuration, persistent evidence, compatibility, execution identity, and experiment manifests first-class.

## Highlights

- Owned `RuntimeOptions`, `Runtime`, and lightweight copyable `ExecutionContext`.
- Context-aware parallel, numerical, scientific, and Vision overloads.
- Backward-compatible free functions through the process-default Runtime.
- Adaptive and Deterministic execution modes.
- Disabled, ReadOnly, and ReadWrite profile access.
- Exact persistent profiles for threshold, AXPY, dot, norm, and stencil 2D.
- Candidate evidence and explicit Approved deployment state.
- Strict canonical JSON, bounded parsing, duplicate-key rejection, SHA-256 integrity, and atomic explicit saving.
- Runtime, workload, profile, operation, and experiment fingerprints.
- Installed calibration, profile, and replay tools.
- Separate-process heat-diffusion calibration, approval, replay, and byte-identical manifest comparison.

## Deterministic contract

Deterministic semantic execution accepts only an exact compatible Approved profile. It replays the saved route, scheduler, worker budget, numerical plan, provider identity, and capability requirements. Missing, Candidate, corrupted, expired, or incompatible evidence fails before destination modification. There is no silent substitution or fallback to Adaptive mode.

## Benchmark and validation evidence

The final Windows/MSVC `31 full` publication passed:

- **24/24** main Release tests;
- **24/24** native-only no-oneTBB/no-OpenCV tests;
- **3/3** oneTBB + OpenCV focused tests;
- **6/6** exact returned source-ZIP tests;
- all installed consumers and CLI replay stages;
- both documentation validations;
- every v1.7 benchmark objective;
- every retained v1.6 correctness, numerical, reproducibility, route-authentication, and performance-sanity gate.

Adaptive first-operation warm start measured **2.600×** faster than fresh cold Adaptive execution, with a 95% interval of **2.500–2.764×**. Deterministic replay measured **1.014×** warm latency, with a **0.959–1.084×** interval. A 1,000-entry profile loaded in **713.304 ms**, with an upper interval bound of **716.005 ms**.

Two fresh replay processes produced byte-identical manifests, the same output digest, nine deterministic replays, and zero adaptive maintenance counters. See the [benchmark report](benchmarks.md) and [validation matrix](validation.md).

An independent Linux/GCC publication also accepted every v1.7 objective and retained v1.6 gate.

## Compatibility

All v1.0–v1.6 APIs and validated kernels remain available. Explicit Runtime instances are isolated from later legacy global-configuration changes. Package target names remain `SmartParallel::smart_parallel` and optional `SmartParallel::vision`.

## Boundaries

- Independent Runtime instances do not share one process-wide CPU governor.
- Cross-architecture or cross-compiler bitwise identity is not promised.
- Concurrent multi-process profile writers are unsupported.
- SHA-256 detects modification but does not authenticate authorship.
- Arbitrary callbacks do not receive persistent semantic identities.
- Public named execution scopes remain deferred.

See [migration](migration.md), [security and trust](security-and-trust.md), and [known limitations](limitations.md).
