# Migrating from v1.0 to v1.1

> **Archived documentation:** SmartParallel v1.0.

Existing calls to `smart::parallel_for(begin, end, callback)` remain source-compatible.

The important behavioral change is nested coordination. In v1.1, nested calls participate in a root execution session, inherit a concurrency budget, and may be deferred below an automatically selected parallel frontier. Applications should not assume that every nested call creates a separate parallel team.

Recommended migration steps:

1. Keep existing loop callsites unchanged.
2. Establish `smart::global_config()` before starting concurrent work.
3. Validate callbacks for exactly-once execution and exception safety.
4. Use nested traces only for diagnostics, not ordinary timing.
5. Re-run workload-specific measurements because the selected frontier and backend can differ from v1.0.
