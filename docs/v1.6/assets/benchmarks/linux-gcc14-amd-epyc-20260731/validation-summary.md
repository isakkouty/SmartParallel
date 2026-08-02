# SmartParallel v1.6 validation summary

- Platform: **Linux / x86_64**
- Compiler: **GCC 14.2**
- CPU: **AMD EPYC 9V74 80-Core Processor**
- Raw benchmark samples: **2,442**
- Evidence schema: **2**
- Execution validity: **Pass**
- Required reference accuracy: **Pass**
- Reproducibility: **Pass**
- Route authentication: **Pass**
- Numerical capability: **Pass**
- Cross-scheduler matrices: **Pass**
- Pointwise plan authentication: **Pass**
- Scientific-kernel performance sanity: **Pass**
- Accurate adversarial sum error: **3000 → 0**
- Accurate adversarial dot error: **3000 → 0**
- Fast compatibility paired median: **1.0634×**
- Fast compatibility 90% interval: **0.9739–1.1611×**
- Fast compatibility status: **not-established**

## Largest Fast workloads versus direct sequential

| Operation | Speedup |
|---|---:|
| axpy | 1.190× |
| dot | 2.346× |
| norm | 2.964× |
| stencil_2d | 3.779× |
| heat_diffusion_20 | 2.142× |

All performance values are machine-specific. The performance-sanity gate is a broad regression detector, not a universal speed claim.
