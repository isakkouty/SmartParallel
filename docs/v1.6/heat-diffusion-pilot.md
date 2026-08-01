# SmartParallel v1.6 heat-diffusion pilot

`examples/v1.6/heat_diffusion.cpp` demonstrates a complete finite-difference-style integration using the new scientific foundation:

- deterministic generated initial field;
- fixed boundary values;
- contiguous `MatrixView` construction;
- five-point stencil calls;
- ping-pong input/output buffers;
- selectable Fast, Reproducible, or Accurate policy;
- independent scalar reference validation for practical validation sizes;
- numerical policy, plan, scheduler, checksum, and application timing output.

Example:

```text
smartparallel_v160_heat_diffusion 512 512 200 reproducible
```

## What it proves

- a real multi-step computation can use non-owning views;
- the same operation can select an explicit numerical policy;
- boundaries remain unchanged;
- result identity can be reported;
- Reproducible and Accurate stencil execution can remain parallel through fixed pointwise tiles.

## Benchmark version

The publication benchmark uses a deterministic nonuniform field, an interior hotspot, varied fixed boundaries, 20 iterations, and complete final-field comparison against an independent direct reference outside the timed region.

The final accepted publication validates the complete field and reports the machine-specific speed relationship against the compact direct-sequential oracle. After moving repeated checked view access out of the inner stencil loop, the accepted Fast, Reproducible, and Accurate ThreadPool paths all outperform that oracle on the retained Linux publication machine. This is evidence for the corrected kernel on that machine, not a universal speed guarantee.

The pilot is not a complete PDE solver, stability analysis, physical material model, or aeronautical certification example.
