# SmartParallel v1.1 nested-execution release notes

## Release contract

The nested module preserves:

- exactly-once callback execution for completed regions;
- first-exception propagation after already-running work reaches a cancellation boundary;
- checked root-session permit release on success and exception;
- no participant oversubscription inside one root session;
- deterministic frozen decisions during one root execution;
- bounded long-running cache, snapshot, and trace retention;
- conservative sequential descendants below the selected frontier;
- consistent exception, permit, and trace contracts across ThreadPool, StaticThread, and oneTBB.

## Final safety hardening

The final v1.1 pass adds:

- generation-safe stable-plan publication;
- cache-clear epoch invalidation and ABA-safe in-flight ownership;
- use-count and wall-clock single-flight revalidation;
- actual-backend trace confirmation;
- exceptional trace cleanup;
- deep nested cancellation/recovery stress;
- multi-level reentrant ThreadPool wait support;
- nested shutdown draining;
- long-running cache churn and bounded retention;
- automated cross-backend comparison.

## Validation gates

The standard release suite contains 13 CTest targets. Local validation used GCC 14.2, Clang 17, AddressSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer. Real oneTBB execution remains a platform-specific gate and is required by the cross-backend validation command.

## Known limitations

- The root budget is per session, not process-global.
- Strict proportional fairness is not guaranteed among a sustained stream of competing roots, although independent external roots retain caller progress and bounded-progress stress passes.
- The strict frontier can leave capacity idle on sufficiently skewed trees.
- Reused functor types require `smart::with_parallel_callsite` when semantically distinct callsites need separate profiles.
- Concurrent mutation of `global_config()` is unsupported; configure before concurrent execution or increment the policy generation between quiescent phases.
- General external cancellation tokens are not part of v1.1.
