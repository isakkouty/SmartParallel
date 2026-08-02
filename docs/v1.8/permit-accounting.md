# SmartParallel v1.8 — Permit-accounting contract

One permit represents one thread actively participating in governed operation work.

- A caller executing operation work consumes one participant.
- A caller waiting for workers does not add another participant.
- Idle ThreadPool workers do not consume operation permits.
- Scheduler capacity and observed participation are reported separately.
- Sequential execution uses one participant.
- Nested work reuses the parent admission and never creates additional root permits.

For governor-native execution, the invariant is:

```text
maximum active governed participation <= declared governor budget
```

Permit ownership is non-preemptive and returned exactly once through lease lifetime.
