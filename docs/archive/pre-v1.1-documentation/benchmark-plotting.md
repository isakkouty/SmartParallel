# Reproducing benchmark figures

`tools/plot_benchmark.py` generates a chart from any benchmark CSV with numeric columns.

```bat
python tools\plot_benchmark.py ^
  validation\output\scientific_test3_irregular_particles.csv ^
  --x case ^
  --y sequential_ms smartparallel_ms ^
  --output docs\v1\assets\particles-runtime.png
```

Useful plots include:

- sequential versus SmartParallel runtime;
- speedup by workload size;
- adaptive regret by case;
- profiling, decision, and execution timing components.

Do not overwrite the original CSV. Figures should state the source file and should not imply that machine-specific values are universal.
