# SmartParallel v1.7 release validation

The release workflow validates correctness, performance evidence, packaging, installed consumption, dependency isolation, cross-process replay, and exact source-archive reproducibility.

## Accepted Windows/MSVC matrix

| Matrix | Result |
|---|---|
| MSVC Release build with oneTBB | **Passed** |
| Complete v1.0–v1.7 CTest regression | **24/24 passed** |
| v1.7 31-repetition benchmark objectives | **All accepted** |
| Retained v1.6 scientific benchmark | **3,936 samples; all gates passed** |
| Documentation checker | **Passed** |
| Installed core consumer | **Passed** |
| Installed profile consumer | **Passed** |
| Installed Vision consumer | **Passed** |
| Installed OpenCV Vision consumer | **Passed** |
| Installed CLI calibration → approval → replay | **Passed** |
| Native-only no-oneTBB/no-OpenCV regression | **24/24 passed** |
| oneTBB + OpenCV focused matrix | **3/3 passed** |
| Exact source-ZIP manifest | **704/704 source entries passed in the accepted archive** |
| Exact source-ZIP focused regression | **6/6 passed** |
| Exact source-ZIP documentation | **Passed** |
| Source ZIP compressed-data integrity | **Passed** |

The accepted source archive SHA-256 was:

```text
aca096c061c99bf9344ae98d632e1906df6b78cd47a2570315daa6024369993d
```

The documentation-only archive returned with the final v1.7 documentation has a new checksum and regenerated manifest; no implementation source is changed.

## Deterministic replay gates

The release requires:

- exact Approved profile compatibility;
- stable Runtime and operation fingerprints;
- byte-identical manifests from two fresh processes;
- identical output digests;
- unchanged Approved profile bytes;
- zero learning, timing, holdout, drift, route-switch, and mutation counters.

## Failure semantics

Correctness, integrity, compatibility, missing runtime dependencies, consumer configuration, manifest mismatches, or exact-ZIP failures stop the release immediately.

Performance handling is evidence-aware:

- `PASS` when the complete interval satisfies the objective;
- `FAIL` when the complete interval violates it;
- `NOT-ESTABLISHED` when the interval crosses the boundary and therefore does not establish a credible regression.

Smoke runs validate correctness and evidence shape but do not apply full publication statistics to underpowered samples.

## Independent Linux validation

The source also passed GCC and Clang release matrices, warnings-as-errors, sanitizer coverage, installed consumers, shared-install CLI checks, exact-ZIP tests, and a complete smoke release workflow. The retained Linux/GCC benchmark publication accepted every v1.7 objective and v1.6 guard.

See [benchmark results](benchmarks.md) and the [reproduction guide](reproduction-guide.md).
