# SmartParallel v1.6 benchmark methodology

The corrected v1.6 publication uses evidence schema 2. It measures floating sum, dot product, norm, AXPY, five-point stencil, and a 20-step heat-diffusion pilot under Fast, Reproducible, and Accurate policies.

## What is being proven

The benchmark asks four separate questions:

1. Did the operation execute safely and produce the expected IEEE classification?
2. Did it match the independent reference where the selected policy is required to do so?
3. Did Reproducible and Accurate produce the same bits across eligible worker counts and scheduler engines?
4. What performance cost or benefit was observed on this specific machine?

It does not assume that Accurate or Reproducible must outperform Fast.

## Evidence fields

Schema 2 deliberately separates:

- `execution_valid`: expected finite, NaN, or infinity classification;
- `reference_accuracy_pass`: operation-specific independent-reference requirement;
- `reproducibility_pass`: repeated bitwise identity under the declared scope;
- `route_authentication_pass`: requested route actually executed;
- `numerical_capability_pass`: selected route satisfied the requested policy;
- `result_digest` and `reference_digest`: complete logical-output identities where applicable.

This prevents a finite but numerically poor adversarial Fast result from being labeled reference-accurate.

## Full-output validation

Correctness is evaluated outside timed regions.

- AXPY compares every logical vector element.
- Stencil compares every logical matrix element, including boundaries and padded layouts.
- Heat diffusion compares the complete final field after deterministic nonuniform initialization and 20 iterations.
- Full-output digests are retained with each relevant sample.
- Scalar reductions record value bits and numerical error.

A single center pixel or selected vector element is not accepted as publication proof.

## Fair timing controls

- deterministic generated inputs;
- identical buffers, shapes, strides, and alignment conditions across policy comparisons;
- preparation and output validation outside timed intervals;
- no output allocation inside timed operations;
- outputs consumed after execution;
- first-call and stable phases separated;
- stable samples summarized with median, p95, p99, minimum, and maximum;
- balanced or adjacent ordering where subtraction is sensitive to cache or runtime drift;
- Release builds without unsafe fast-math options;
- raw evidence retained before summary generation.

## Cross-scheduler matrices

The publication includes three reproducibility matrices:

- `sum_scaling` for canonical reductions;
- `axpy_pointwise_matrix` for fixed one-dimensional pointwise tiles;
- `stencil_pointwise_matrix` for fixed two-dimensional pointwise tiles.

Eligible ThreadPool, StaticThread, and oneTBB routes are exercised across worker budgets 1, 2, 4, and 8 where available. The result bits/digests must remain identical, and budgets above one must authenticate real parallel execution where enough work exists.

## Scientific-kernel performance sanity

The analyzer compares the largest Fast workload for AXPY, dot, norm, five-point stencil, and the 20-iteration heat pilot with their compact direct-sequential references. Every operation must remain at least **0.5×** the direct reference. This deliberately broad threshold is a regression detector: it catches catastrophic implementation overhead such as repeated checked indexing in an inner loop, but it does not assert that SmartParallel must win or that results transfer to other machines.

## Fast compatibility gate

Policy-aware Fast is compared with the retained legacy Fast overload through adjacent alternating samples. This avoids the previous order bias caused by timing all policy-aware calls before all legacy calls.

A ratio above 1.05 is an investigation trigger, not an assertion that every noisy microbenchmark must be within exactly 5%.

## Accepted publication

The corrected accepted Linux/GCC/x86-64 publication used 21 stable repetitions and produced **2,442 raw rows**.

- all release gates passed;
- Accurate adversarial sum and dot error fell from 3000 to 0;
- policy-aware Fast / retained Fast had a paired median of **1.0634×**, with a 90% robust interval of **0.9739–1.1611×**, producing an **inconclusive-pass**;
- all three cross-scheduler matrices passed;
- both pointwise plan identities were authenticated.

The historical Windows schema-v1 run is preserved only for traceability. A current Windows/MSVC schema-v2 run is also retained: 3,936 samples and all benchmark gates passed. The workflow stopped only before its isolated no-oneTBB matrix because of the corrected LF-only batch-label defect; therefore the benchmark and main MSVC claims are supported, while the Windows no-oneTBB claim still requires a rerun.
