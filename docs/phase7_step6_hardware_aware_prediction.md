# Phase 7 Step 6: hardware-aware prediction

The analytical predictor now derives a bounded hardware context for every
parallel candidate. It models per-worker L2 pressure, whole-workload L3
pressure, SMT oversubscription, locality and NUMA pressure, then limits
unrealistic parallel scaling for memory-sensitive workloads. The correction is
bounded and configurable, so machine calibration and measured experience remain
the stronger signals when sufficient evidence exists.
