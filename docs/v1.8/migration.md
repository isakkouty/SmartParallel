# Migration from v1.7

Existing free functions and Runtime construction remain source compatible. A Runtime without an explicit governor uses the process-default governor.

`worker_budget` remains accepted; `maximum_workers` is the explicit v1.8 local ceiling. Existing v1.7 profiles are not silently reinterpreted. Resource-contract fields missing from old profiles are adapted only where their meaning is unambiguous; deterministic compatibility rejects ambiguous or incompatible contracts.

Applications that create multiple Runtimes should pass one shared explicit governor when they want a common process budget.
