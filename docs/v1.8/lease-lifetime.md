# SmartParallel v1.8 — Lease lifetime and exception safety

A granted root lease remains valid until its owning operation releases or destroys it. It is immutable after grant and non-preemptive.

Required lifetime properties:

- copying is disabled;
- moving transfers ownership;
- destruction is `noexcept`;
- user callbacks never run under the governor mutex;
- provider configuration never runs under the governor mutex;
- exceptions release permits;
- inherited leases keep required parent state alive without owning root permits;
- shutdown wakes pending requests but does not invalidate active leases.

Restoration failures for process-global providers are reported separately because destructors cannot safely throw.
