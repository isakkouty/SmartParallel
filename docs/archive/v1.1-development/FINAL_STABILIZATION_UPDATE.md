# SmartParallel v1.1 Final Stabilization Update

This is the final correction pass after the isolated build-171 real-world run.
It intentionally avoids new scheduling research and changes only four mechanisms
whose causes were directly visible in the benchmark data. The public API, CMake
options, vcpkg manifest, NMake/MSVC workflow, target names, and Windows batch
entry points remain unchanged.

## 1. Root tiny-work bypass now requires structural and profitability evidence

The absolute-cost bypass no longer redirects every stable sub-millisecond root.

- Descendants beneath an already sealed frontier may still execute directly.
- A root may use the absolute-cost bypass only when it contains one coarse item,
  exposes nested work, and its measured profile predicts sequential execution.
- Profitable multi-item roots remain eligible for ThreadPool or oneTBB even when
  their total runtime is below one millisecond.
- Explicitly forced backends remain unaffected.

This preserves the particle `tiny` optimization while preventing the build-171
OpenCV `tiny` and BVH `small_uniform` regressions.

## 2. Steady-state profile revalidation freezes after warm-up

`FunctionProfileCache` now has an explicit, thread-safe revalidation freeze.

- Production revalidation is enabled by default.
- The real-world harness clears and unfreezes learning before each mode.
- Cold and warm-up executions may learn and revalidate normally.
- After warm-up, timed repetitions and the representative steady-state trace
  cannot acquire an online revalidation pass.
- Clearing the profile cache restores the normal production state.

This separates steady-state latency from periodic learning cost without removing
SmartParallel's production adaptation behavior. Existing irregular and
production-stress tests continue to validate that revalidation works when the
freeze is not active.

## 3. CPU metrics are gated by observed process-time resolution

The benchmark now measures the smallest positive process CPU-time increment once
outside timed regions. A timed batch is reported only after accumulating at
least eight observed timer quanta and remaining within a tight physical worker
bound.

Short or quantized batches are marked unavailable instead of reporting
impossible equivalent-core values. Substantial workloads continue to report:

- process CPU equivalent cores,
- normalized machine CPU utilization,
- accumulated timed-batch CPU seconds,
- accumulated timed-batch wall time.

Environment metadata records the observed timer quantum and the required number
of quanta.

## 4. Markdown analysis generation is mandatory and explicit

The PowerShell comparator writes the Markdown report through the .NET file API
using UTF-8 without a BOM. Both the comparator and the complete batch command
verify that:

- `v1.1.0_real_world_analysis.md` exists,
- the file is non-empty,
- the complete command fails immediately if either condition is false.

Combined numeric CSV exports continue to use invariant culture.

## Schema

The environment schema is now version 4 and adds:

- `profile_revalidation_timed_phase=frozen_after_warmup`
- `profile_revalidation_production_default=enabled`
- `cpu_metric_semantics=process_cpu_equivalent_cores_quantum_gated_v4`
- `cpu_metric_observed_timer_quantum_seconds=<measured>`
- `cpu_metric_minimum_timer_quanta=8`

Existing raw, summary, and trace columns remain compatible.

## Final Windows command

From the extracted project root:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```
