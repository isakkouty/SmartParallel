# SmartParallel v1.8 — Resource decision reports

Each governed operation can report its operation and Runtime identity, governor budget, Runtime ceiling, minimum/preferred/maximum request, requested and granted workers, scheduler cap, observed participation, admission result, wait policy, nesting mode, scheduler/provider identity, control capabilities, deterministic requirement, and rejection reason.

Stable execution fields are separated from volatile diagnostics such as wait duration, lease sequence identity, queue state, timestamps, process IDs, and thread IDs.
