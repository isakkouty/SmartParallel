# Phase 7 Step 2 — Workload Similarity Normalization

SmartParallel now compares workload fingerprints in normalized logarithmic
space instead of dividing encoded bucket integers directly.

This matters because input sizes, working sets, object sizes, and callback
costs span several orders of magnitude. A multiplicative change now has a
consistent meaning regardless of absolute scale, and the encoded
floating-point buckets used for function cost and variation are decoded before
comparison.

The similarity report now also exposes evidence coverage and normalized
distance. Missing features no longer count as perfect matches: they reduce
coverage and conservatively reduce the final transferable similarity.

Incompatible workload kinds still receive zero similarity. Exact fingerprint
identity and exact execution history remain stronger than all similarity-based
transfer.
