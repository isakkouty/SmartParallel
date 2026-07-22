# SmartParallel v1.3 release checklist

> **Release:** SmartParallel v1.3.0

This checklist separates correctness/portability validation from manual performance validation.

## Before merging

- Confirm the pull request targets `main`.
- Confirm the latest commit—not an older cancelled run—has six green GitHub Actions jobs.
- Confirm `macos-appleclang-release-tbb` passed the 16 deterministic tests, installation, and external consumer.
- Confirm no real-world benchmark result, graph, or CSV was regenerated as part of the CI release.
- Review the changed-file list and verify scheduler policy and benchmark algorithms were not modified for documentation-only follow-up changes.

## Merge

For a pull request containing several CI-fix commits, **Squash and merge** produces a clean release commit. A suitable title is:

```text
feat: add SmartParallel v1.3 cross-platform portability
```

Delete the feature branch after merging when it is no longer needed.

## After merging

- Open **Actions → CI** and wait for the automatic `main` run.
- Confirm all six jobs pass again on the exact commit now present on `main`.
- Open **Actions → Management → Caches** and confirm vcpkg binary caches exist for the oneTBB-enabled jobs. Cache restoration avoids rebuilding compatible packages, although each hosted job still starts on a fresh runner and performs a lightweight restore/install step.
- Configure a ruleset under **Settings → Rules → Rulesets** to require the CI checks before future merges.

## Publish v1.3.0

After the `main` workflow is green:

1. Create the annotated tag `v1.3.0` on the release commit.
2. Push the tag.
3. Create a GitHub Release from `v1.3.0`.
4. Use [`release-notes.md`](release-notes.md) as the release-description basis.
5. Attach source archives only when they are intentionally produced and checksum-verified.

Example command-line tagging workflow:

```text
git switch main
git pull --ff-only
git tag -a v1.3.0 -m "SmartParallel v1.3.0"
git push origin v1.3.0
```

## Performance evidence

The v1.3 CI result is evidence of cross-platform compilation, deterministic correctness, installation, package consumption, oneTBB integration, and sanitizer cleanliness. It is not evidence that all operating systems have identical performance.

The checked-in v1.1 benchmark report remains the current performance reference. New Windows, Linux, or macOS performance claims require manual runs on controlled physical machines using the documented benchmark methodology.
