# SmartParallel v1.7 accepted Linux validation summary

- Complete GCC Release regression: **24/24 passed**.
- Explicit GCC no-oneTBB/no-OpenCV matrix: **24/24 passed**.
- Clang 17 warnings-as-errors matrix: **24/24 passed**.
- Clang 17 AddressSanitizer + UndefinedBehaviorSanitizer matrix: **24/24 passed** with leak detection and halt-on-error.
- Install-tree consumers: core Runtime, scientific profile, and Vision profile consumers all passed.
- Documentation and script line-ending validation passed.
- Separate-process strided heat pilot produced byte-identical manifests, identical operation fingerprints, identical output digests, and zero adaptive-maintenance counters during Deterministic replay.
- Approved ReadOnly profile bytes remained unchanged across both replay processes.
- v1.7 paired benchmark analysis found no statistically credible objective failure at 31 repetitions. Warm-start, Deterministic latency, and profile-scale gates passed; the two sub-millisecond API-overhead intervals were `INCONCLUSIVE-PASS`.
- v1.6 scientific regression correctness/reproducibility gates passed at 11 repetitions; Fast kernel speedups over direct sequential were 1.242× AXPY, 2.692× dot, 2.848× norm, 3.607× stencil, and 1.811× heat diffusion on this machine.

Not executed in this environment: MSVC, Apple Clang, oneTBB route execution, OpenCV route execution, and an accepted ThreadSanitizer run. These are not claimed as passed.
