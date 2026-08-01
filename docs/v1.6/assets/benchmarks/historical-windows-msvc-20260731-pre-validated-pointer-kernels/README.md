# Historical Windows/MSVC schema-v2 evidence — before validated pointer kernels

This retained run completed the corrected Windows release workflow on an AMD Ryzen 7 2700X:

- 3,936 schema-v2 samples;
- 20/20 main tests;
- 20/20 isolated no-oneTBB/no-OpenCV tests;
- core and Vision installed consumers;
- documentation validation;
- all numerical, reproducibility, authentication, and pointwise-plan gates.

It is **historical performance evidence only** because it predates the final validated pointer/stride scientific kernels and the scientific-kernel performance-sanity gate. The run exposed the repeated checked-`View` inner-loop cost: heat diffusion was correct but approximately 11.5× slower than the compact direct-sequential oracle.

Do not use its timing values as current v1.6 performance claims. Rerun the final source and publish the resulting platform evidence with `tools/publish_v16_benchmark_docs.py`.
