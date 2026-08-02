# Nested lease propagation

A nested SmartParallel operation must never perform an independent blocking acquisition from the same governor while its parent holds permits.

Lease identity is propagated through `ExecutionContext`, not only thread-local state, so migrated worker tasks retain the active resource contract.

v1.8 supports parent reuse and sequential-within-parent behavior. The `PartitionParent` diagnostic value is retained for schema compatibility, but no unsafe public concurrent partition API is exposed. Concurrent sibling partitioning remains internal until strict delegated-capacity accounting can be proven for all scheduler paths.

Validation covers depths one through four, exceptions, migrated work, exactly-once output, one root grant, and peak child participation no greater than the parent grant.
