# SmartParallel v1.1 nested-execution release notes

## Release contract

The nested module preserves:

- exactly-once callback execution for completed regions;
- first-exception propagation after already-running helpers retire;
- checked root-session permit release on success and exception;
- no participant oversubscription inside one root session;
- deterministic frozen decisions during one root execution;
- bounded long-running cache, snapshot, and trace retention;
- the conservative sequential descendant path below the selected frontier.

## Final production hardening

The final v1.1 pass adds:

- policy-sensitive stable-plan invalidation;
- single-flight periodic revalidation;
- decaying nested-shape evidence and root-grouped observations;
- bounded least-recently-used profile retention;
- bounded root snapshots and global trace records;
- explicit reusable-functor callsite keys;
- StaticChunks participation in session lease accounting;
- exception-safe StaticThread partial construction;
- oneTBB arena width constrained by acquired permits;
- overflow-safe scheduler chunk arithmetic;
- randomized long-running production stress.

## Validation gates

The standard release suite contains 12 CTest targets. In addition to regular frontier and hardening tests, the production stress target covers randomized irregular nesting, concurrent roots, repeated exceptions, cache/trace pressure, revalidation races, StaticThread paths, and near-limit scheduler arithmetic.

Local validation used GCC 14.2, Clang 17, AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer. Real oneTBB execution remains a platform-specific gate and can be required explicitly with the supplied validation script.

## Known limitations

- The root budget is per session, not process-global.
- Strict fairness is not guaranteed among sustained competing roots.
- The strict frontier can leave capacity idle on sufficiently skewed trees.
- Reused functor types require `smart::with_parallel_callsite` when semantically distinct callsites need independent profiles.
- Concurrent mutation of `global_config()` is unsupported.
- General external cancellation tokens are not part of v1.1.
