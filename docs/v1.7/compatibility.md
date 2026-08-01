# SmartParallel v1.7 exact compatibility rules

Deterministic replay is exact and fail-closed. A profile is usable only when the saved identity matches the current operation, workload, numerical contract, build, environment, and available execution capabilities.

## Compatibility groups

### Schema and semantics

- profile schema and semantic version;
- operation name and operation semantic version;
- element type;
- plan semantic version.

### Numerical identity

- numerical policy;
- evaluation order;
- accumulation algorithm;
- canonical plan;
- numerical capability requirements.

### Workload identity

- exact workload fingerprint;
- extents and strides;
- layout and in-place state;
- boundary mode;
- semantic constants such as AXPY alpha, threshold values, or stencil coefficients.

### Environment and build identity

- architecture, pointer width, and endianness;
- operating-system family;
- compiler, standard library, build type, and SmartParallel build fingerprint;
- floating-point environment;
- application build identifier;
- required ISA and feature macros.

### Execution capability

- exact worker budget and worker policy;
- scheduler availability;
- provider identity, version, and settings;
- SIMD kernel and capability availability.

### Trust and integrity

- Approved state;
- evidence gates;
- expiry;
- entry and database SHA-256 integrity.

## Adaptive behavior

Adaptive mode may inspect compatible Candidate or Approved entries as restart evidence. An incompatible entry is ignored as authoritative evidence and current-context learning continues. The incompatible evidence does not silently become the selected exact plan.

## Deterministic behavior

Deterministic mode reports structured compatibility issues and fails before destination modification. It does not:

- substitute another scheduler;
- change worker count;
- switch provider;
- loosen workload matching;
- accept Candidate evidence;
- fall back to Adaptive mode.

This strictness is the core reproducibility contract. See [workload identity](workload-identity.md), [profile schema](profile-schema.md), and [security boundaries](security-and-trust.md).
