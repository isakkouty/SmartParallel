# Historical Windows/MSVC v1.6 pre-correction evidence

This directory preserves the Windows/MSVC evidence produced before the final v1.6 release-candidate correction.

It is retained for engineering traceability only. It used evidence schema 1, the reduction leaf plan for pointwise operations, and single-point publication checks for AXPY, stencil, and heat diffusion. It must not be quoted as final v1.6 evidence.

Generate current Windows/MSVC schema-v2 evidence with:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v16_scientific_foundations_release_validation.bat 31
```
