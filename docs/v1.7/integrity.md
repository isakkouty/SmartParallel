# SmartParallel v1.7 profile integrity model

v1.7 uses strict canonical JSON and SHA-256 to detect content changes.

## Canonical integrity

- JSON is parsed with bounded size and depth.
- Duplicate keys are rejected.
- Numeric overflow and malformed fields are rejected.
- Accepted content is serialized in one canonical representation.
- Each profile entry has an SHA-256 identity.
- The database has an SHA-256 identity over canonical content excluding its own hash field.

Equivalent accepted JSON is normalized before hashing, so formatting differences do not create ambiguous semantic identities.

## Detected failures

Integrity validation detects:

- changed or truncated bytes;
- malformed JSON;
- duplicate keys;
- missing required fields;
- inconsistent entry hashes;
- an inconsistent database hash;
- semantic modification of a signed-by-hash field.

## Trust boundary

SHA-256 does not prove:

- who created or approved the profile;
- that the software or machine is uncompromised;
- that a party capable of replacing the file cannot also replace the hash;
- regulatory or safety certification.

Cryptographic signatures and organizational authorization systems are outside v1.7.

Store Approved profiles in a controlled ReadOnly location, retain calibration evidence separately, and compare the expected database hash during deployment.
