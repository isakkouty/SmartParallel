# Generated v1.5 benchmark results

> Generated from the accepted Windows/MSVC publication evidence. Do not edit by hand; rerun `tools/publish_v15_benchmark_docs.py`.

- Publication: `publication_20260728_192748`
- Raw samples: **2,238**
- Correctness/authentication failures: **0**
- Combined proof gates: **6/6 passed**
- Native kernel: **avx2**
- Geometric-mean Auto speedup versus direct sequential: **1.17×**
- Geometric-mean Auto speedup versus direct OpenCV API: **1.49×**

| Preset | Settled route | Auto median | Speedup vs direct | Speedup vs OpenCV API | Route regret | Runtime switches |
|---|---|---:|---:|---:|---:|---:|
| 320×240 | Native Sequential | 2.7 µs | 0.96× | 1.78× | 0.00% | 0 |
| 640×480 | Native Sequential | 9.3 µs | 1.06× | 2.86× | 0.00% | 0 |
| 1920×1080 | Native Sequential | 87.7 µs | 1.12× | 1.59× | 0.00% | 1 |
| 1920×1080 ROI | Native Sequential | 92.8 µs | 1.05× | 1.34× | 0.00% | 1 |
| 3840×2160 | Native ThreadPool | 669.3 µs | 1.90× | 1.02× | 0.00% | 0 |
| 7680×4320 | Native ThreadPool | 4.335 ms | 1.14× | 1.00× | 0.96% | 0 |

All figures and aggregate values are machine-specific. The release claim is that Auto selected a route inside the declared equivalence gate on this machine, not that one provider is universally fastest.
