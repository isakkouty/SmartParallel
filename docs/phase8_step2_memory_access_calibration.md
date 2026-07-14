# Phase 8 Step 2: Specialized memory access calibration

This step replaces the remaining one-size-fits-all memory correction with four bounded regimes:

- cache-resident regular access;
- bandwidth-bound contiguous streaming;
- latency-bound random or pointer-based access;
- large-record streaming.

The regime classifier uses structural access metadata, L3 pressure, record size, and workload-family confidence. Each calibration curve has separate worker and backend behavior, and its effect is blended by confidence so ambiguous classifications cannot dominate the analytical model.

The implementation is intentionally conservative. It adjusts predicted execution cost before residual calibration and ranking, while preserving the existing bandwidth-saturation policy from Step 1.
