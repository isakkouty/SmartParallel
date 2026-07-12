# Decision Engine

The decision engine converts workload information into an `ExecutionPlan`. It is the policy layer between the public algorithms and the execution backends.

## Inputs

A decision receives:

- a `Workload`
- a `WorkloadAnalysis`
- optional `ExecutionHints`
- an optional `FunctionProfile`

These values are collected in a `DecisionContext` and passed to `CompositeDecisionProvider`.

## Output

The engine returns an `ExecutionPlan` containing the selected engine, scheduling strategy, job count, and parallel flag. A richer `DecisionReport` is retained for diagnostics.

## Fallback

When no provider produces a report, the engine returns a safe sequential plan with one job. This makes failure to recommend a plan explicit and deterministic.

## Function cost and workload size

Beta 1.0 uses both the size of the iteration space and sampled function behavior. This is why the tiny-versus-heavy benchmark can choose sequential execution for a tiny callback and oneTBB for an expensive callback at the same container size.

The model is still heuristic. The memory-bandwidth benchmark demonstrates that a large iteration count does not guarantee that parallel execution is profitable when each iteration performs only a trivial read/modify/write operation.

## Historical decisions

The experience subsystem can provide measured historical information through a history-oriented provider. Experience entries include confidence derived from sample count and timing stability.

Historical data should be treated as advisory because workload fingerprints do not capture every environmental factor, such as background load, frequency scaling, or memory placement.

## Planned improvements

- predicted total time for each candidate plan
- adaptive worker count
- automatic chunk/grain-size selection
- explicit memory-bound classification
- stronger use of hardware topology
- confidence-aware sampling and fallback
