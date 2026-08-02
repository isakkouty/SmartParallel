# SmartParallel v1.8 — ResourceGovernor

`ResourceGovernor` is an explicit process-level admission authority independent of any Runtime.

```cpp
auto governor = std::make_shared<smart::ResourceGovernor>(
    smart::ResourceGovernorOptions{8});
```

A Runtime may share that governor while retaining isolated configuration, profiles, adaptive evidence, and numerical defaults.

The governor owns permit accounting, the pending queue, bounded-bypass fairness state, cancellation wakeups, shutdown state, and admission diagnostics. It does not own scientific data, numerical plans, profiles, or provider implementations.

The configured budget is checked against an effective-capacity report. Linux uses the process affinity mask and available cgroup quota information. Windows uses process affinity and reports a conservative diagnostic when multiple processor groups cannot be represented safely by the current process mask.
