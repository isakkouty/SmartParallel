param(
    [string]$OutputDirectory = "validation\output"
)

$ErrorActionPreference = "Stop"

function Require([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Row-Key($Row) {
    return "$($Row.suite)|$($Row.case)|$($Row.configuration)"
}

try {
    $summaryFiles = [ordered]@{
        thread_pool = "v1.1.0_nested_execution_optimized.csv"
        static_thread = "v1.1.0_nested_execution_optimized_static_run.csv"
        one_tbb = "v1.1.0_nested_execution_optimized_tbb_run.csv"
    }
    $traceFiles = [ordered]@{
        thread_pool = "v1.1.0_nested_execution_optimized_trace_run_trace.csv"
        static_thread = "v1.1.0_nested_execution_optimized_static_trace_run_trace.csv"
        one_tbb = "v1.1.0_nested_execution_optimized_tbb_trace_run_trace.csv"
    }

    $summaries = @{}
    foreach ($backend in $summaryFiles.Keys) {
        $path = Join-Path $OutputDirectory $summaryFiles[$backend]
        Require (Test-Path $path) "missing validation output: $path"
        $rows = @(Import-Csv $path)
        Require ($rows.Count -gt 0) "empty summary for $backend"
        $map = @{}
        foreach ($row in $rows) {
            Require ($row.engine -eq $backend) "$backend summary contains engine $($row.engine)"
            Require ($row.correct -eq "1") "$backend summary contains a correctness failure"
            Require ($row.checksum -eq $row.expected_checksum) "$backend checksum mismatch"
            $map[(Row-Key $row)] = $row
        }
        $summaries[$backend] = $map
    }

    $baselineKeys = @($summaries.thread_pool.Keys | Sort-Object)
    foreach ($backend in $summaryFiles.Keys) {
        $keys = @($summaries[$backend].Keys | Sort-Object)
        Require (($keys -join "`n") -eq ($baselineKeys -join "`n")) `
            "$backend summary does not contain the same workload/configuration set"
    }

    foreach ($key in $baselineKeys) {
        $expected = $summaries.thread_pool[$key].expected_checksum
        foreach ($backend in $summaryFiles.Keys) {
            Require ($summaries[$backend][$key].expected_checksum -eq $expected) `
                "expected checksum differs for $key under $backend"
        }
    }

    foreach ($backend in $traceFiles.Keys) {
        $path = Join-Path $OutputDirectory $traceFiles[$backend]
        Require (Test-Path $path) "missing detailed trace: $path"
        $rows = @(Import-Csv $path)
        $parallel = @($rows | Where-Object { $_.parallel -eq "1" })
        Require ($parallel.Count -gt 0) "$backend trace contains no parallel records"
        foreach ($row in $parallel) {
            Require ($row.backend_confirmed -eq "1" -and $row.backend -eq $backend) `
                "$backend trace does not prove the requested backend executed"
        }
        foreach ($row in $rows) {
            Require ([int64]$row.max_root_leased_workers -le 4) `
                "$backend trace exceeded the four-participant validation budget"
            if ($backend -ne "thread_pool") {
                Require ([int64]$row.helpers_submitted -eq 0) `
                    "$backend trace leaked ThreadPool dependency helpers"
            }
        }
    }

    $comparison = foreach ($key in $baselineKeys) {
        $base = $summaries.thread_pool[$key]
        [pscustomobject]@{
            suite = $base.suite
            case = $base.case
            configuration = $base.configuration
            parallel_levels = $base.parallel_levels
            dimensions = $base.dimensions
            thread_pool_median_ms = $base.median_ms
            static_thread_median_ms = $summaries.static_thread[$key].median_ms
            one_tbb_median_ms = $summaries.one_tbb[$key].median_ms
            checksum = $base.checksum
        }
    }
    $comparisonPath = Join-Path $OutputDirectory "v1.1.0_nested_execution_cross_backend_comparison.csv"
    $comparison | Export-Csv -Path $comparisonPath -NoTypeInformation
    Write-Host "Cross-backend validation: PASS"
    Write-Host "Comparison written: $comparisonPath"
    exit 0
}
catch {
    Write-Error "Cross-backend validation: FAIL: $($_.Exception.Message)"
    exit 1
}
