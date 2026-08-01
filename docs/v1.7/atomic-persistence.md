# SmartParallel v1.7 atomic profile persistence

Profile saving is explicit and transactional at the single-file level.

## Save sequence

`save_profile_database_atomic` performs the following steps:

1. Serialize the database as canonical JSON.
2. Compute and include per-entry and database SHA-256 identities.
3. Write a temporary file in the destination directory.
4. Flush and close the temporary file.
5. Reopen, parse, and validate the temporary content.
6. Verify canonical integrity before replacement.
7. Atomically replace the destination.
8. Remove temporary files through scoped cleanup.

Same-directory replacement is required so the temporary and destination files reside on the same filesystem.

## Platform behavior

- POSIX platforms use same-directory atomic rename semantics.
- Windows uses `MoveFileExW` with replacement and write-through flags.

The Windows validation keeps all destination streams closed before replacement, matching normal Runtime and tool behavior.

## Guarantees

- A failed temporary write or validation does not overwrite the previous destination.
- The final destination is always a fully validated canonical database.
- Operations never trigger profile-file writes.
- Runtime destruction does not implicitly save.

## Non-guarantees

- Concurrent multi-process writers are unsupported.
- Atomic replacement is not a distributed transaction.
- Filesystem durability after abrupt hardware failure remains subject to the operating system and storage device.
- SHA-256 detects changed content but does not prove authorship.

Use one designated writer, keep deployment profiles ReadOnly, and retain the previous Approved file when promoting new evidence.
