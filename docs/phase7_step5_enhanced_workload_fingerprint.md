# Phase 7 Step 5: enhanced workload fingerprint

The workload identity now captures access topology, stride, cache pressure,
machine topology and profile-shape information in addition to size and callback
cost. Exact identity remains deterministic, while similarity transfer now
penalizes workloads that look alike in size but differ in locality or access
pattern. This reduces unsafe transfer between streaming, node-based and random
access workloads.
