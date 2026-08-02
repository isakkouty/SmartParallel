# SmartParallel v1.8 — Security and trust boundaries

The governor is a concurrency-accounting mechanism, not a security sandbox. It cannot stop unrelated code from creating threads, changing process-global provider state, or consuming CPU outside SmartParallel.

SHA-256 detects evidence modification but does not prove authorship. Deterministic replay authenticates the supported execution contract, not operating-system scheduling or hard real-time behavior.
