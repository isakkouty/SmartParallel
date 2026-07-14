# Phase 2 Predictive Validation Suite

The Phase 2 unit tests prove that the predictive and calibration algorithms behave according to their contracts. They do not prove that predicted runtimes or plan choices match real execution.

The new `validation/` suite closes that gap by measuring every generated candidate plan on controlled cheap, compute-heavy, irregular, and mixed workloads. It records candidate-level prediction error, predicted-winner accuracy, and selected-plan regret without enabling predictive control.

This baseline must be collected before adaptive workers and chunk sizes are introduced. Future model changes should be judged by whether they reduce decision regret and prediction error across the validation matrix rather than merely improving one benchmark.
