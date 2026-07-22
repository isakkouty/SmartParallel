# Nested parallelism

> **Current documentation:** SmartParallel v1.1.0.

## Problem

Naively nesting parallel loops can create a full worker team at every level, oversubscribe the machine, deadlock a saturated pool, or spend more time scheduling descendants than executing useful work.

## Root execution session

The first automatic loop creates a root session with a bounded concurrency budget. Nested loops inherit:

- root and parent loop identity;
- runtime ownership;
- available budget;
- cancellation and exception state;
- frontier state;
- plan snapshots and trace context.

Participant leases reserve actual execution width. The recorded root lease maximum is the authoritative budget-accounting signal.

## Automatic frontier

SmartParallel chooses a parallel frontier in the nested call tree. Work at the frontier may execute in parallel; descendants normally take a sealed sequential fast path. This avoids repeated full scheduling decisions and prevents recursive oversubscription.

The strict frontier is intentionally conservative. It can leave some capacity unused on highly skewed trees, but it provides deterministic resource bounds and strong safety behavior.

## Backend coordination

- ThreadPool uses dependency-local cooperative helping so workers can make progress while nested work completes.
- oneTBB executes in an arena constrained to the acquired width and reuses an existing arena only when its concurrency is compatible.
- StaticThread creates a bounded fixed team and joins partial teams safely on failure.
- Exhausted or incompatible descendants fall back to sequential execution.

## Exceptions and cancellation

An exception in a descendant sets the shared cancellation state. Work that has not started is suppressed where the backend supports it, participants release leases, helpers retire, and the original exception is rethrown by the root call.
