# SmartParallel v1.7 profile and replay tools

The v1.7 tools make evidence production, review, approval, and replay explicit.

## Profile tool

```text
smartparallel_profile inspect PROFILE
smartparallel_profile validate PROFILE
smartparallel_profile approve CANDIDATE APPROVED
smartparallel_profile compare PROFILE_A PROFILE_B
```

### `inspect`

Prints entries, status, numerical policy, route, scheduler, worker budget, sample count, workload fingerprint, and entry hash.

### `validate`

Strictly parses canonical JSON and verifies entry/database integrity.

### `approve`

Revalidates Candidate evidence and writes a distinct Approved database. This is the only supplied Candidate-to-Approved transition.

### `compare`

Reports database-hash equality and meaningful entry-level differences.

## Calibration tool

```text
smartparallel_calibrate MANIFEST.json
```

Produces Candidate evidence plus raw, summary, metrics, environment, compatibility, fingerprint, hash, and report artifacts. See [offline calibration](calibration.md).

## Replay tool

```text
smartparallel_replay run APPROVED.json MANIFEST.json ROWS COLUMNS ITERATIONS WORKERS SEED [ROW_STRIDE]
smartparallel_replay compare MANIFEST_A MANIFEST_B
```

`run` loads an Approved profile ReadOnly in Deterministic mode and writes a stable heat-diffusion manifest. `compare` requires stable-manifest equality.

## Recommended command sequence

```sh
smartparallel_calibrate calibration.json
smartparallel_profile inspect evidence/candidate_profile.json
smartparallel_profile validate evidence/candidate_profile.json
smartparallel_profile approve evidence/candidate_profile.json evidence/approved_profile.json
smartparallel_profile validate evidence/approved_profile.json
smartparallel_replay run evidence/approved_profile.json replay-a.json 64 64 8 2 170
smartparallel_replay run evidence/approved_profile.json replay-b.json 64 64 8 2 170
smartparallel_replay compare replay-a.json replay-b.json
```

Every command returns a failing status on invalid input or failed validation. The Windows release workflow additionally verifies expected output creation so a loader or dependency failure cannot cascade into a misleading later error.
