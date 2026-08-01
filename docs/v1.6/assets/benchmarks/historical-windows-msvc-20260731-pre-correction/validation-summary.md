# Windows/MSVC v1.6 validation summary

This evidence was generated on July 31, 2026 with MSVC 19.44, oneTBB 2023.0.0, and OpenCV 4.12.0.

| Gate | Result |
|---|---:|
| Primary MSVC/oneTBB/OpenCV CTest regression | 20/20 passed |
| Core installed consumer | 1/1 passed |
| Vision/OpenCV installed consumer | 1/1 passed |
| Explicit oneTBB-disabled/OpenCV-disabled regression | 20/20 passed |
| Documentation checker | PASS |
| Benchmark gate fields | 3,104/3,104 passed |
| Reproducibility matrix | PASS |
| Accurate adversarial sum error | 3000 → 0 |
| Accurate adversarial dot error | 3000 → 0 |
| Policy-aware Fast / retained-Fast largest-sum median | 1.0335×, PASS |

The 1.0335× ratio is within the release's 5% investigation threshold. Performance values are machine-specific. The evidence does not establish cross-compiler, cross-binary, cross-architecture, safety-critical, hard-real-time, or universal-performance guarantees.
