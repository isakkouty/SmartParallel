# SmartParallel scientific benchmarks

The scientific suite validates SmartParallel on deterministic numerical workloads using only C++17, SmartParallel, and its existing oneTBB dependency.

## Test 1: numerical integration

Approximates the integral of `sin(x) * exp(-0.1*x)` over `[0, 100]` with the midpoint rule. Each interval writes one independent contribution; a deterministic sequential reduction is performed outside the timed region.

Run from the repository root:

```bat
benchmarks\scientific\scripts\run_scientific_test1.bat
```

To override the vcpkg toolchain path:

```bat
benchmarks\scientific\scripts\run_scientific_test1.bat "D:\vcpkg\scripts\buildsystems\vcpkg.cmake"
```

Results are written to `validation/output/scientific_test1_numerical_integration.csv`.

## Test 2: 2D heat diffusion

Runs a deterministic explicit heat-diffusion simulation using a five-point stencil and fixed boundary conditions. It covers 128x128 through 2048x2048 grids and validates the complete output field against the sequential implementation.

Run only Test 2:

```bat
benchmarks\scientific\scripts\run_scientific_test2.bat
```

Results are written to `validation/output/scientific_test2_heat_diffusion.csv`.

Run every currently implemented scientific benchmark:

```bat
benchmarks\scientific\scripts\run_scientific_suite.bat
```

## Test 3: irregular particle simulation

Evaluates a deterministic particle-energy kernel with strongly non-uniform work per particle. Each particle performs between 16 and 511 inner iterations, which exercises SmartParallel's adaptive scheduling on an irregular compute-bound workload.

Run only Test 3:

```bat
benchmarks\scientific\scripts\run_scientific_test3.bat
```

Results are written to `validation/output/scientific_test3_irregular_particles.csv`.

