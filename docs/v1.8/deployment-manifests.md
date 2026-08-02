# Deployment manifests

v1.8 extends experiment manifests with governor configuration, process budget, operation resource fingerprints, request bounds, exact grants, nesting decisions, provider-control mode, deterministic admission policy, oversubscription-prevention decisions, and input/output digests.

Volatile diagnostics are stored separately and do not change the stable manifest identity. This includes queue wait time, queue depth, wall-clock times, process ID, and lease sequence identity.
