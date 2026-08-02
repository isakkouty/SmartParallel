# Deterministic resource admission

Deterministic execution remains fail-closed.

Before output modification, SmartParallel validates the Approved profile, governor compatibility, exact worker grant, scheduler/provider availability, control strength, numerical policy, and canonical plan.

A smaller grant is not substituted. The scheduler is not changed. Adaptive learning does not run. Failure leaves destination data and ReadOnly profiles unchanged.

v1.8 distinguishes execution-identity determinism, exact resource-admission determinism, and wall-clock queue behavior. It guarantees the first two under compatible conditions; it does not guarantee identical queue order or wait duration.
