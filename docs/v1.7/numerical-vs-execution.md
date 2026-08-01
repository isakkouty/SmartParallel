# SmartParallel v1.7 numerical policy versus execution mode

Execution mode and numerical policy answer different questions.

- **Execution mode:** may the Runtime learn or change the plan?
- **Numerical policy:** what evaluation order and floating-point algorithm does the operation use?

## Contract matrix

| Combination | Execution identity | Numerical behavior |
|---|---|---|
| Adaptive + Fast | May learn and change | Retained Fast behavior; bitwise identity is not promised. |
| Adaptive + Reproducible | May learn route/plan subject to the policy | v1.6 canonical numerical decomposition. |
| Adaptive + Accurate | May learn route/plan subject to capability | v1.6 accurate numerical algorithm where supported. |
| Deterministic + Fast | Replays exact Approved route/plan | Fast may still permit floating-point variation allowed by its contract. |
| Deterministic + Reproducible | Replays exact Approved route/plan | Exact plan plus canonical reproducible numerical decomposition. |
| Deterministic + Accurate | Replays exact Approved route/plan | Exact plan plus the approved accurate algorithm. |

A Deterministic route does not make Fast arithmetic bitwise reproducible. Conversely, Reproducible arithmetic does not prevent Adaptive mode from changing a compatible execution route between calls.

Reports and fingerprints record both dimensions independently, including evaluation order, accumulation algorithm, canonical plan, execution mode, route, scheduler, and worker identity.
