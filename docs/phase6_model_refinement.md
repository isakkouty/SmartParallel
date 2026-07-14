# Phase 6A: workload-family classification

Phase 6A introduces a conservative, diagnostic workload-family classifier.
It does not change execution-plan selection yet.

The classifier combines only observations SmartParallel can already justify:

- explicit execution hints when the caller supplied them;
- structural storage, contiguity, random-access, stride, object-size, and cache-pressure observations;
- optional function-profile variability, tail, and regional-cost observations.

The published families are:

- `ComputeHeavy`
- `StreamingMemory`
- `IrregularMemory`
- `BranchHeavy`
- `Mixed`
- `Unknown`

Every classification includes a confidence, per-family evidence scores, and flags describing which observation sources contributed. Ambiguous cases intentionally collapse to `Mixed` rather than pretending to know more than the sensors support.

This phase is the foundation for family-specific calibration, residual correction, uncertainty-aware blending, and bounded knowledge transfer in later Phase 6 updates.
