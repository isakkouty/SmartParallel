# SmartParallel v1.6 execution architecture

v1.6 adds numerical contracts without replacing the established Fast scheduler paths. The central design rule is that **numerical structure and scheduler choice are separate decisions**.

## Three public policies

```text
Operation + NumericalOptions
        |
        +-- Fast
        |     existing adaptive/native execution
        |
        +-- Reproducible
        |     fixed numerical plan
        |
        +-- Accurate
              fixed numerical plan + stronger supported arithmetic
```

Existing overloads remain Fast. Numerical policy is explicit per call and is not stored in mutable process-global configuration.

## Deterministic reductions

Sum, dot, and norm require partial-result merging. Reproducible and Accurate routes use a canonical structure:

```text
Input range
    ↓
Fixed 1024-element leaves
    ↓
Schedulers may execute leaf indices in any order
    ↓
Each leaf writes its assigned partial slot
    ↓
Fixed binary merge tree
    ↓
Final result
```

For a given operation semantic version and input length, worker count and scheduler timing cannot change:

- leaf boundaries;
- element order inside a leaf;
- partial-result index;
- merge-tree shape;
- merge order;
- initial-value treatment.

Sequential Reproducible and parallel Reproducible execution therefore use the same mathematical decomposition.

Current reduction plans are:

- `canonical-pairwise-v1-leaf1024`;
- `canonical-neumaier-v1-leaf1024`;
- `canonical-scaled-sumsq-v1-leaf1024`.

## Deterministic pointwise operations

AXPY and stencil do not merge partial numerical states. Reusing the reduction leaf plan for them would be conceptually wrong and could collapse a large 2D grid into one sequential row group.

v1.6 therefore uses separate pointwise plans:

```text
Logical vector or matrix
    ↓
Fixed tiles derived only from logical shape
    ↓
Schedulers may execute tiles in any order
    ↓
Each output element is written exactly once
    ↓
Per-element expression order remains fixed
```

Current plan IDs are:

- `canonical-pointwise-v1-target4096` for one-dimensional pointwise work;
- `canonical-pointwise-2d-v1-target4096` for row-tiled two-dimensional work.

The target controls deterministic tile formation; it does not depend on available workers. This permits Reproducible and Accurate AXPY/stencil operations to remain parallel while preserving their fixed element expression.

## Internal numerical dimensions

Public presets map to independent internal concepts:

- `EvaluationOrder::{Adaptive, CanonicalDeterministic, CanonicalPointwise}`;
- `AccumulationMethod::{Native, FixedPointwiseExpression, CanonicalPairwise, Compensated, ScaledSumOfSquares}`.

A numerical execution report authenticates:

- operation;
- requested policy;
- evaluation order;
- accumulation method;
- plan identity;
- requested and actual scheduler;
- configured and actual workers;
- route execution;
- numerical capability satisfaction.

## Capability boundary

Numerical capability belongs to the operation implementation, not to a scheduler name. ThreadPool or oneTBB does not make a computation Accurate. The compensated or scaled algorithm scheduled through that engine provides the numerical capability.

A candidate incapable of satisfying the requested policy must be rejected before execution. Silent fallback from Accurate to Fast is forbidden.

## Allocation boundary

Canonical reductions currently allocate a bounded partial-state vector. Pointwise plans do not require a reduction-state vector. v1.6 does not promise allocation-free Reproducible or Accurate reductions; caller-provided workspaces remain future work.

## Preserved architecture

The following remain unchanged:

- v1.0 adaptive indexed execution;
- v1.1 nested coordination and participant budgets;
- v1.4 algorithm hot dispatch and Fast behavior;
- v1.5 semantic Vision routes and optional OpenCV isolation;
- package target names and core-only dependency isolation.
