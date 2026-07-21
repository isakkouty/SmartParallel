# SmartParallel v1.1.0 nested-execution benchmarks

This suite now separates three different questions:

1. `all_sequential`: correctness and serial baseline.
2. `forced_all_levels`: scheduler stress with every selected level manually forced onto the ThreadPool.
3. `automatic_all_levels`: the real public `smart::parallel_for` automatic policy at every depth.

The four-level configuration matrix also includes one manually parallel level at a time and the `flattened_nd` fast path.

Every timed row validates a deterministic checksum. The suite writes median/min/max summaries, raw repetition samples, and an optional automatic-policy trace.

## One-command validation

From the repository root:

Windows:

```bat
scripts\validation\run_nested_release_validation.bat 11
```

Linux/macOS:

```bash
./scripts/validation/run_nested_release_validation.sh 11
```

To record a diagnostic trace:

```bat
scripts\validation\run_nested_release_validation.bat 11 trace
```

```bash
./scripts/validation/run_nested_release_validation.sh 11 trace
```

## Direct benchmark invocation

The benchmark executable accepts:

```text
smartparallel_v110_nested_benchmarks <summary.csv> <repetitions> [trace]
```

Outputs beside the requested summary CSV:

- `<stem>.csv`: summary rows.
- `<stem>_raw.csv`: every repetition in execution order.
- `<stem>_trace.csv`: structured automatic-policy trace; it contains only a header unless `trace` was requested.

Automatic cases perform two untimed warm-ups: one for exactly-once cold telemetry and one for stable-plan establishment.

## Interpreting results

Performance is machine-dependent. The release gates are:

- all checksums pass;
- no deadlock or timeout;
- the root trace does not exceed its configured worker budget;
- automatic runs do not return to the former multi-millisecond wake-up tails;
- repeated automatic plans remain stable after warm-up.

Tracing changes the timing of tiny loops. Use normal runs for performance and a separate three-repetition trace run for diagnosis.
