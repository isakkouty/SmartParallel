# SmartParallel v1.8 — Documentation correction validation

This report covers the final release-documentation and evidence corrections applied after the hardened v1.8 implementation review.

## Change boundary

A file-by-file comparison against the uploaded hardened archive found:

- **0 C++ source changes**;
- **0 public-header changes**;
- **0 CMake changes**;
- changes limited to README/CHANGELOG text, v1.8 release reports, the benchmark analyzer, the documentation validator, and regenerated Linux figures/metadata.

The benchmark analyzer now emits a cross-platform figure only when a second raw dataset is explicitly supplied. The Linux accepted evidence contains fourteen genuine Linux publication figures and no Windows placeholder.

## Validation rerun

Because the compiled implementation was unchanged, the existing hardened build trees were rerun against the same C++ and CMake inputs:

| Matrix | Result |
|---|---:|
| GCC complete v1.0–v1.8 suite | **26/26 passed** |
| Clang warnings-as-errors focused v1.8 suite | **2/2 passed** |
| AddressSanitizer/UndefinedBehaviorSanitizer focused v1.8 suite | **2/2 passed** |
| ThreadSanitizer focused v1.8 suite | **2/2 passed** |
| Python packaging/analyzer syntax | **passed** |
| Documentation validation | **passed** |
| v1.8 mandatory benchmark gates | **9/9 passed** |
| Linux publication plot count | **14/14** |

The retained negative performance comparisons remain unchanged and visible.

## Remaining platform action

The final Windows/MSVC `31 full` publication must still be run on the intended Windows release host. This is required both for Windows acceptance and for generation of the real Windows-versus-Linux comparison figure.
