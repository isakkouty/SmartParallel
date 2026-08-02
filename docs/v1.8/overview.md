# Overview

SmartParallel v1.8 adds a process-level admission authority around the execution paths that participate in SmartParallel governance.

The release addresses five deployment failures:

1. Independent Runtime instances reserving overlapping CPU capacity.
2. Nested parallel operations independently acquiring another root budget.
3. Deterministic replay silently receiving a smaller resource grant.
4. External runtimes being described more strongly than their APIs permit.
5. Resource decisions being impossible to explain after execution.

The implementation centers on `ResourceGovernor`, move-only `ExecutionLease` objects, operation-specific concurrency requests, direct cancellation notification, bounded-bypass fairness, effective CPU-capacity diagnostics, and stable resource reports.

v1.8 deliberately excludes new external application integrations. The roadmap resumes application pilots in v1.9.
