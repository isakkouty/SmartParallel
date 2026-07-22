# Benchmark methodology

## Principles

The benchmark suite separates **correctness**, **execution speed**, **scheduler overhead**, and **decision quality**. Results should be generated in Release mode on an otherwise quiet machine. Multiple repetitions are aggregated by each benchmark to reduce noise.

## Computer-vision workloads

- **Threshold:** very cheap per-pixel comparison and assignment; intentionally exposes dispatch overhead.
- **5x5 convolution:** regular stencil computation with meaningful arithmetic per pixel.
- **Sobel:** gradient stencil with regular memory access and moderate arithmetic.
- **Stress suite:** Mandelbrot microtiles, adaptive quadtree analysis, Monte Carlo image sampling, and other irregular or mixed image kernels.

OpenCV benchmarks compare direct sequential loops, OpenCV parallel facilities or optimized primitives where applicable, and SmartParallel. Correctness uses checksums and maximum numerical differences.

## Scientific workloads

- **Numerical integration:** compute-heavy independent intervals with an analytical reference.
- **Heat diffusion:** repeated 2D stencil updates and substantial memory traffic.
- **Irregular particles:** variable per-particle work intended to reward dynamic load balancing.

## Decision-quality audit

Each case is executed using forced sequential, forced oneTBB, and adaptive selection. The fastest measured forced backend is the oracle for that run.

```text
adaptive_regret = adaptive_ms / min(sequential_ms, forced_onetbb_ms)
```

- `1.0` is oracle-equivalent.
- Above `1.0` measures the cost of a suboptimal choice plus adaptive framework overhead.
- `decision_correct` compares backend identities, not whether output is correct.
- `output_correct` verifies numerical equivalence.

## Limitations of interpretation

Recorded timing is specific to one machine, compiler, dependency set, thermal state, and system load. Tiny timings are especially noisy. The suite demonstrates behavior and regression trends; it does not claim universal speedups.
