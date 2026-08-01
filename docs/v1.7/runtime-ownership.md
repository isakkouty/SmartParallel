# SmartParallel v1.7 Runtime ownership and lifetime

`Runtime` makes execution state explicit and locally owned.

## Construction snapshot

`RuntimeOptions` are validated and snapshotted during construction. Later changes to `smart::global_config()` do not mutate an explicit Runtime.

The Runtime owns:

- scheduler configuration and capability identity;
- effective worker budget and numerical default;
- profile access policy and in-memory database;
- adaptive evidence and telemetry;
- build, hardware, provider, and floating-environment identity;
- Runtime and last-operation fingerprints.

Adaptive evidence is mutable but synchronized and Runtime-local.

## Context ownership

`Runtime::context()` returns a shared-state handle. A copied context keeps the state alive even if the original wrapper is destroyed. Copying does not create a new scheduler or database.

## Concurrency

One Runtime supports concurrent calls. Its shared evidence and telemetry are synchronized, while the existing scheduler and nested coordinator enforce the Runtime’s operation-level execution contract.

Separate Runtime instances do not share profiles or configuration snapshots.

## Resource boundary

v1.7 does not introduce a process-wide governor across independent Runtimes. Two Runtimes configured with large worker budgets may collectively oversubscribe the machine. Applications that create multiple Runtimes must budget them deliberately.

## Recommended patterns

- One long-lived Runtime per independent execution policy or deployment identity.
- Copy contexts into components that need execution access.
- Use separate Runtimes when configurations must be isolated.
- Avoid constructing a new Runtime per small operation unless the construction itself is under test.
- Use ReadOnly Approved evidence in deployment and a separate ReadWrite Runtime for calibration.
