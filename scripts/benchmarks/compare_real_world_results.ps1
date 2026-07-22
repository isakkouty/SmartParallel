param(
    [string]$OutputDirectory = "validation\output\real_world"
)

$ErrorActionPreference = "Stop"
function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Expected-Behavior([string]$Integration, [string]$Preset) {
    switch ($Integration) {
        "opencv" {
            switch ($Preset) {
                "tiny" { return "Sequential should win; automatic should avoid creating a parallel frontier." }
                "one_large" { return "Inner tile parallelism or flattened tile execution should be strongest." }
                "few_large" { return "Either image-level or tile-level parallelism may win depending on image balance." }
                "many_medium" { return "Outer image parallelism should usually be sufficient." }
                "thousands_small" { return "Coarse outer batching should beat repeated inner scheduling." }
                default { return "Mixed sizes should require a conservative adaptive frontier rather than all-level parallelism." }
            }
        }
        "lz4" {
            if ($Preset -like "tiny_*") { return "Sequential or a very low-overhead flat strategy should win." }
            if ($Preset -eq "mixed_sizes") { return "Parallel block scheduling should help, but skew and bandwidth may limit scaling." }
            return "Parallel execution across independent blocks should improve throughput until bandwidth saturates."
        }
        "bvh" {
            if ($Preset -eq "small_uniform") { return "Sequential should win because recursive scheduling overhead dominates." }
            if ($Preset -eq "highly_unbalanced") { return "A recursive frontier should help, but branch skew may leave measurable regret." }
            return "A bounded recursive frontier should outperform sequential construction without recursive oversubscription."
        }
        "particles" {
            if ($Preset -eq "tiny") { return "Sequential should win and automatic should remain sequential." }
            if ($Preset -eq "sudden_count_change" -or $Preset -eq "gradual_count_increase" -or $Preset -eq "moving_clusters") {
                return "Repeated frames should expose warm-plan reuse and workload-drift adaptation."
            }
            return "Outer cell or flattened particle parallelism should help while inner-only scheduling may be too fine-grained."
        }
    }
    return "No expectation recorded."
}

function Regret-Assessment([double]$AbsoluteMs, [double]$Percentage) {
    if ($Percentage -le 15.0) { return "close_to_best" }
    if ($AbsoluteMs -lt 1.0) { return "tiny_absolute_regret" }
    if ($Percentage -le 50.0) { return "measurable_regret" }
    return "large_regret_review_trace"
}

try {
    $integrations = @("opencv", "lz4", "bvh", "particles")
    $allRows = @()
    $allRaw = @()
    $environmentByIntegration = @{}
    $traceByIntegration = @{}
    $summaryColumns = $null
    $traceColumns = $null

    foreach ($integration in $integrations) {
        $prefix = "v1.1.0_real_world_${integration}"
        $summaryPath = Join-Path $OutputDirectory "${prefix}_summary.csv"
        $rawPath = Join-Path $OutputDirectory "${prefix}_raw.csv"
        $environmentPath = Join-Path $OutputDirectory "${prefix}_environment.csv"
        $tracePath = Join-Path $OutputDirectory "${prefix}_trace.csv"
        Require (Test-Path $summaryPath) "missing summary: $summaryPath"
        Require (Test-Path $rawPath) "missing raw repetitions: $rawPath"
        Require (Test-Path $environmentPath) "missing environment metadata: $environmentPath"
        Require (Test-Path $tracePath) "missing trace output: $tracePath"

        $rows = @(Import-Csv $summaryPath)
        $rawRows = @(Import-Csv $rawPath)
        $traceRows = @(Import-Csv $tracePath)
        $environmentRows = @(Import-Csv $environmentPath)
        Require ($rows.Count -gt 0) "empty summary: $summaryPath"
        Require ($rawRows.Count -gt 0) "empty raw CSV: $rawPath"
        Require ($traceRows.Count -gt 0) "empty trace CSV; run the suite with trace enabled: $tracePath"

        $currentSummaryColumns = @($rows[0].PSObject.Properties.Name)
        $currentTraceColumns = @($traceRows[0].PSObject.Properties.Name)
        if ($null -eq $summaryColumns) { $summaryColumns = $currentSummaryColumns }
        else { Require (($summaryColumns -join '|') -eq ($currentSummaryColumns -join '|')) "$integration summary schema differs" }
        if ($null -eq $traceColumns) { $traceColumns = $currentTraceColumns }
        else { Require (($traceColumns -join '|') -eq ($currentTraceColumns -join '|')) "$integration trace schema differs" }

        $environment = @{}
        foreach ($item in $environmentRows) { $environment[$item.key] = $item.value }
        Require ($environment.ContainsKey("smartparallel_version")) "$integration environment lacks SmartParallel version"
        Require ($environment.ContainsKey("compiler")) "$integration environment lacks compiler"
        Require ($environment.ContainsKey("cpu_model")) "$integration environment lacks CPU model"
        Require ($environment.ContainsKey("random_seed")) "$integration environment lacks random seed"
        $environmentByIntegration[$integration] = $environment
        $traceByIntegration[$integration] = $traceRows

        foreach ($trace in $traceRows) {
            Require ([int64]$trace.max_root_leased_workers -le 4) `
                "$integration trace exceeded the configured four-worker budget"
            if ($trace.parallel -eq "1") {
                Require ($trace.backend_confirmed -eq "1") `
                    "$integration trace contains an unconfirmed parallel backend"
                if ($trace.mode -like "*_thread_pool") {
                    Require ($trace.backend -eq "thread_pool") "ThreadPool mode executed $($trace.backend)"
                }
                elseif ($trace.mode -like "*_static_thread") {
                    Require ($trace.backend -eq "static_thread") "StaticThread mode executed $($trace.backend)"
                    Require ([int64]$trace.helpers_submitted -eq 0) "StaticThread trace leaked ThreadPool helpers"
                }
                elseif ($trace.mode -like "*_one_tbb") {
                    Require ($trace.backend -eq "one_tbb") "oneTBB mode executed $($trace.backend)"
                    Require ([int64]$trace.helpers_submitted -eq 0) "oneTBB trace leaked ThreadPool helpers"
                }
            }
        }

        foreach ($row in $rows) {
            Require ($row.correct -eq "1" -and $row.valid_for_ranking -eq "1") `
                "$integration contains an invalid benchmark result: $($row.preset)/$($row.mode)"
            Require ($row.checksum -eq $row.expected_checksum) `
                "$integration checksum mismatch: $($row.preset)/$($row.mode)"
            Require ([int64]$row.max_concurrency -le [int64]$row.workers) `
                "$integration exceeded its worker limit: $($row.preset)/$($row.mode)"
            if ($row.actual_backend -ne "sequential") {
                $matchingParallel = @($traceRows | Where-Object {
                    $_.preset -eq $row.preset -and $_.mode -eq $row.mode -and
                    $_.parallel -eq "1" -and $_.backend_confirmed -eq "1"
                })
                Require ($matchingParallel.Count -gt 0) `
                    "$integration summary claims backend $($row.actual_backend) without confirmed trace evidence: $($row.preset)/$($row.mode)"
            }
        }
        foreach ($raw in $rawRows) {
            Require ($raw.correct -eq "1" -and $raw.checksum -eq $raw.expected_checksum) `
                "$integration raw repetition failed correctness: $($raw.preset)/$($raw.mode)/$($raw.repetition)"
        }
        $allRows += $rows
        $allRaw += $rawRows
    }

    $comparison = foreach ($row in $allRows) {
        [pscustomobject]@{
            integration = $row.integration
            workload = $row.workload
            preset = $row.preset
            mode = $row.mode
            requested_backend = $row.requested_backend
            actual_backend = $row.actual_backend
            selected_strategy = $row.selected_strategy
            selected_frontier = $row.selected_frontier
            cold_ms = [double]$row.cold_ms
            median_ms = [double]$row.median_ms
            p95_ms = [double]$row.p95_ms
            p99_ms = [double]$row.p99_ms
            throughput_per_second = [double]$row.throughput_per_second
            speedup_over_sequential = [double]$row.speedup_over_sequential
            absolute_regret_ms = [double]$row.absolute_regret_ms
            percentage_regret = [double]$row.percentage_regret
            max_concurrency = [int64]$row.max_concurrency
            scheduler_decisions = [int64]$row.scheduler_decisions
            cache_hits = [int64]$row.cache_hits
            stable_plan_reuse = [int64]$row.stable_plan_reuse
            correct = $row.correct
        }
    }
    $comparisonPath = Join-Path $OutputDirectory "v1.1.0_real_world_comparison.csv"
    $comparison | Export-Csv -Path $comparisonPath -NoTypeInformation

    $analysisRows = @()
    foreach ($integration in $integrations) {
        $presets = @($comparison | Where-Object { $_.integration -eq $integration } |
                     Select-Object -ExpandProperty preset -Unique | Sort-Object)
        foreach ($preset in $presets) {
            $caseRows = @($comparison | Where-Object { $_.integration -eq $integration -and $_.preset -eq $preset })
            $best = $caseRows | Sort-Object median_ms | Select-Object -First 1
            $sequential = $caseRows | Where-Object { $_.mode -eq "sequential" } | Select-Object -First 1
            $automatic = $caseRows | Where-Object { $_.mode -eq "smart_auto" -or $_.mode -eq "smart_auto_frontier" } | Select-Object -First 1
            Require ($null -ne $best -and $null -ne $sequential -and $null -ne $automatic) `
                "missing best, sequential, or automatic row for $integration/$preset"
            $analysisRows += [pscustomobject]@{
                integration = $integration
                preset = $preset
                expected_behavior = (Expected-Behavior $integration $preset)
                best_mode = $best.mode
                best_backend = $best.actual_backend
                best_median_ms = [double]$best.median_ms
                sequential_median_ms = [double]$sequential.median_ms
                automatic_backend = $automatic.actual_backend
                automatic_frontier = $automatic.selected_frontier
                automatic_cold_ms = [double]$automatic.cold_ms
                automatic_median_ms = [double]$automatic.median_ms
                automatic_p95_ms = [double]$automatic.p95_ms
                automatic_speedup = [double]$automatic.speedup_over_sequential
                automatic_regret_ms = [double]$automatic.absolute_regret_ms
                automatic_regret_percent = [double]$automatic.percentage_regret
                cold_to_warm_ratio = $(if ([double]$automatic.median_ms -gt 0.0) { [double]$automatic.cold_ms / [double]$automatic.median_ms } else { 0.0 })
                scheduler_assessment = (Regret-Assessment ([double]$automatic.absolute_regret_ms) ([double]$automatic.percentage_regret))
            }
        }
    }
    $analysisCsvPath = Join-Path $OutputDirectory "v1.1.0_real_world_auto_analysis.csv"
    $analysisRows | Export-Csv -Path $analysisCsvPath -NoTypeInformation

    $analysisPath = Join-Path $OutputDirectory "v1.1.0_real_world_analysis.md"
    $lines = @(
        "# SmartParallel v1.1 real-world integration analysis",
        "",
        "Generated from the complete, non-cherry-picked CSV result set. Correctness failures are excluded by failing the comparator before this report is written.",
        "",
        "| Integration | Preset | Expected behavior | Best mode | Auto median ms | Auto speedup | Auto regret | Auto backend | Frontier | Assessment |",
        "|---|---|---|---|---:|---:|---:|---|---|---|"
    )
    foreach ($row in ($analysisRows | Sort-Object integration,preset)) {
        $regret = "$([math]::Round($row.automatic_regret_ms, 4)) ms / $([math]::Round($row.automatic_regret_percent, 2))%"
        $lines += "| $($row.integration) | $($row.preset) | $($row.expected_behavior) | $($row.best_mode) | $([math]::Round($row.automatic_median_ms, 4)) | $([math]::Round($row.automatic_speedup, 3))x | $regret | $($row.automatic_backend) | $($row.automatic_frontier) | $($row.scheduler_assessment) |"
    }
    $lines += ""
    $lines += "## Interpretation rules"
    $lines += ""
    $lines += "- `close_to_best`: automatic median is within 15% of the fastest valid mode."
    $lines += "- `tiny_absolute_regret`: relative regret is high but the absolute difference is below 1 ms."
    $lines += "- `measurable_regret`: automatic remains correct but a manual strategy is materially faster."
    $lines += "- `large_regret_review_trace`: inspect the trace and workload shape; the report does not automatically label variance or policy regret as a scheduler defect."
    $lines += ""
    $lines += "## Environment"
    $lines += ""
    foreach ($integration in $integrations) {
        $env = $environmentByIntegration[$integration]
        $lines += "- **$integration**: SmartParallel $($env['smartparallel_version']); $($env['compiler']); $($env['operating_system']); CPU $($env['cpu_model']); workers $($env['selected_worker_limit']); seed $($env['random_seed'])."
    }
    $lines += ""
    $lines += "Every available execution mode remains in the comparison CSV, including cases where sequential, a manual frontier, a backend-specific plan, or flattened execution wins."
    Set-Content -Path $analysisPath -Value $lines -Encoding UTF8

    Write-Host "Real-world comparison: PASS"
    Write-Host "Comparison written: $comparisonPath"
    Write-Host "Automatic analysis written: $analysisCsvPath"
    Write-Host "Narrative analysis written: $analysisPath"
    exit 0
}
catch {
    Write-Error "Real-world comparison: FAIL: $($_.Exception.Message)"
    exit 1
}
