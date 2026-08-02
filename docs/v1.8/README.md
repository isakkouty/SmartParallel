# SmartParallel v1.8.0 — Governed Scientific Execution

> **Trust the deployment.**

SmartParallel v1.8 coordinates participating CPU execution paths inside one process. It admits operations through an explicit process budget, prevents nested operations from independently expanding that budget, preserves deterministic exact-resource requirements, and records the stable and volatile evidence needed to explain each decision.

The contract is cooperative rather than universal: unrelated application threads, external processes, memory, GPUs, NUMA placement, and direct calls into non-participating libraries remain outside the governor.

## Quick start

```cpp
smart::ResourceGovernorOptions governor_options;
governor_options.cpu_budget = 8;
auto governor = std::make_shared<smart::ResourceGovernor>(governor_options);

smart::RuntimeOptions first_options;
first_options.governor = governor;
first_options.maximum_workers = 6;
smart::Runtime first(first_options);

smart::RuntimeOptions second_options;
second_options.governor = governor;
second_options.maximum_workers = 4;
smart::Runtime second(second_options);
```

Adaptive operations request operation-specific useful concurrency. The request distinguishes minimum, preferred, and maximum workers. Deterministic operations continue to require the exact approved grant and fail before output modification when it is unavailable.

## Documentation map

### Contract and architecture

- [Overview](overview.md)
- [Trust-the-deployment contract](trust-the-deployment.md)
- [ResourceGovernor](resource-governor.md)
- [Permit accounting](permit-accounting.md)
- [Execution leases](execution-leases.md)
- [Lease lifetime and exception safety](lease-lifetime.md)
- [Worker-field semantics](worker-semantics.md)

### Admission and coordination

- [Admission policies](admission-policies.md)
- [Deadlines and cancellation](deadline-cancellation.md)
- [Fairness](fairness.md)
- [Multi-Runtime coordination](multi-runtime.md)
- [Nested leases](nested-leases.md)
- [Deterministic admission](deterministic-admission.md)

### Backends and evidence

- [Governor-native and constrained execution](governor-native-vs-constrained.md)
- [oneTBB governance](onetbb-governance.md)
- [OpenCV containment](opencv-containment.md)
- [OpenMP status](openmp.md)
- [Provider-control capability model](provider-control.md)
- [Resource reports](resource-reports.md)
- [Resource fingerprints](resource-fingerprints.md)
- [Deployment manifests](deployment-manifests.md)

### Validation and release

- [Oversubscription methodology](oversubscription-methodology.md)
- [Accepted benchmark evidence](accepted-benchmark-evidence.md)
- [Release validation status](validation-status.md)
- [Release confidence and readiness](release-confidence.md)
- [Cleanup report](cleanup-report.md)
- [Exact-archive validation](exact-archive-validation.md)
- [Documentation correction validation](correction-validation.md)
- [Migration from v1.7](migration.md)
- [Security and trust boundaries](security.md)
- [Known limitations](limitations.md)
- [Reproduction](reproduction.md)
- [Release notes](release-notes.md)

## Release boundary

v1.8 contains process-level CPU governance only. Rodinia HotSpot and all new application integrations are deferred to v1.9 and are not included in the v1.8 source artifact, build, installation, tests, benchmarks, plots, or release claims.
