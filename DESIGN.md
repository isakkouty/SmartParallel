# Decision Architecture

Production path:

`Workload + observations + execution hints -> DecisionContext -> decision providers -> ExecutionPlan`

Runtime prediction is not invoked by `DecisionEngine`.

Offline validation measures every candidate, computes regret, trains a pairwise utility scorer, and compares it against the frozen legacy baseline. Model promotion requires sufficient independent workloads, lower mean regret, and no higher catastrophic-decision rate.
