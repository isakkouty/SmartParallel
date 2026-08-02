# SmartParallel v1.8 — oneTBB task-arena governance

SmartParallel executes its oneTBB route inside `oneapi::tbb::task_arena` with the lease grant as the arena concurrency cap.

The cap is an upper bound, not a private worker reservation. SmartParallel does not claim governance of unrelated oneTBB tasks, exact worker participation, or exclusive scheduler ownership.

Reports record the requested and granted workers, arena cap, observed participation, oneTBB identity, `PerTask` control scope, and `UpperBound` control strength. Deterministic replay authenticates the arena cap and provider identity rather than worker identities.
