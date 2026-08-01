# SmartParallel v1.7 experiment run manifests

The supplied replay tool writes a stable schema-v1 JSON manifest for the heat-diffusion experiment.

## Manifest fields

### Application and experiment

- application name and version;
- rows, columns, row stride, and iteration count;
- boundary mode;
- numerical policy;
- deterministic seed and worker budget.

### Data identity

- input digest;
- output digest;
- final norm identity.

### Runtime and profile identity

- Runtime fingerprint;
- profile database hash;
- operation fingerprints for stencil 2D and norm.

### Execution state

- selected routes and schedulers;
- actual worker counts;
- canonical plans and accumulation algorithms;
- provider/SIMD identities;
- warm-start and Deterministic flags;
- telemetry counters;
- completion status.

## Stable identity

Process-specific details such as timestamps, process IDs, addresses, thread IDs, output paths, and durations are excluded. The sidecar `.sha256` file identifies the exact manifest bytes.

## Accepted Windows evidence

The two final release manifests are byte-identical and hash to:

```text
caa94172f51f4a161658ed39fff102340186ea6f3bba4f327a5a3fa2694e898c
```

Both record output digest:

```text
b10ced4bee0617873433aff5e2ea369135e9b48163924eb1ff249aec6756d3d3
```

They contain two stable operation fingerprints, nine deterministic replays, zero adaptive maintenance, and `completion_status: passed`.

See the retained [`replay-a.json`](assets/benchmarks/windows-msvc-20260801/cross-process/replay-a.json), [`replay-b.json`](assets/benchmarks/windows-msvc-20260801/cross-process/replay-b.json), and [comparison output](assets/benchmarks/windows-msvc-20260801/cross-process/compare.txt).
