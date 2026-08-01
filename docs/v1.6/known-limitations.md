# SmartParallel v1.6 known limitations

- Bitwise reproducibility is not promised across different binaries, compilers, architectures, floating-point environments, or operation/plan versions.
- Unsafe fast-math modes are outside the Reproducible and Accurate contract.
- Accurate arithmetic exists only for supported meaningful operations. Arbitrary custom reductions do not gain an invented accurate method.
- Accurate AXPY and stencil share the Reproducible fixed pointwise expression; no stronger local-error claim is made.
- Canonical reductions use bounded temporary allocation. Caller-provided workspaces are not yet supported.
- Views support host memory only. GPU/device residency and asynchronous memory domains are not modeled.
- General overlap detection for arbitrary strided views can be unknown; operations enforce conservative alias contracts.
- Scientific APIs and numerical reports are experimental and do not carry an ABI-stability promise.
- The heat-diffusion pilot is a readable integration example, not a complete PDE solver, material model, stability proof, or aeronautical tool.
- Scientific-kernel speedups are machine-specific. The accepted publication passes a broad regression-sanity gate and records speedups for its largest workloads, but no universal superiority over compact sequential, OpenMP, oneTBB, or specialist libraries is promised.
- Persistent profiles, owned Runtime instances, deterministic deployment replay, process-wide resource governance, NUMA/affinity, OpenMP, BLAS, FFT, GPU, MPI, C/Python bindings, and signed execution evidence remain future work.
- SmartParallel is not safety certified, hard real time, or approved for flight-critical use.
