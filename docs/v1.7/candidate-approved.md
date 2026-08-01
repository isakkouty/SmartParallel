# SmartParallel v1.7 Candidate and Approved profiles

v1.7 separates measured evidence from deployment authorization.

## Trust states

| State | Produced by | Adaptive use | Deterministic use |
|---|---|---|---|
| `Candidate` | Calibration, adaptive learning, or explicit export | May provide authenticated restart warm-start evidence | Rejected |
| `Approved` | Explicit `smartparallel_profile approve` transition | May provide warm-start evidence | Required for exact semantic replay |

## Candidate evidence

A Candidate entry records the measured route, scheduler, worker plan, numerical contract, workload and environment identities, sample count, median, variability, confidence, holdout, correctness, route authentication, numerical capability, and source calibration identity.

Candidate means “reviewable evidence exists.” It does not mean the organization has authorized the plan for deterministic deployment.

## Approval

`smartparallel_profile approve CANDIDATE APPROVED`:

1. Parses the Candidate database strictly.
2. Validates canonical content and SHA-256 integrity.
3. Requires sufficient sample, confidence, holdout, route-authentication, numerical-capability, and correctness evidence.
4. Writes a new Approved database with new hashes.
5. Preserves each Candidate source hash for traceability.

Calibration never approves silently, and approval never mutates the Candidate file in place.

## Deterministic enforcement

Deterministic semantic operations reject Candidate entries before destination modification. They also reject Approved entries that fail exact compatibility, integrity, evidence, or expiry checks.

## Operational recommendation

Retain three artifacts separately:

- the raw calibration evidence;
- the immutable Candidate profile reviewed by humans or policy;
- the promoted Approved profile distributed to deployment.

This keeps measurement, authorization, and execution auditable.
