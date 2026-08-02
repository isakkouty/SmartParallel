# SmartParallel v1.8 — Oversubscription benchmark methodology

The publication benchmark measures real callback participation under concurrent Runtime execution.

The governed condition uses one shared governor. The ungoverned control uses separate governors with the same per-Runtime ceiling, allowing combined participating execution to exceed effective CPU capacity when the scheduler and machine permit it. No synthetic worker counters are used.

Each pair receives untimed warmups and alternating measurement order. Raw records preserve the order, repetition, randomization seed, operation count, elapsed time, throughput numerator and denominator, request bounds, grant, scheduler cap, observed participants, wait time, correctness, and output digest.

Throughput is defined as completed Runtime operations divided by paired batch elapsed seconds. Performance comparisons use paired bootstrap 95% confidence intervals. Negative and inconclusive results remain visible.
