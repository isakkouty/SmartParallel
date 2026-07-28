# GitHub Actions and vcpkg setup

> **CI baseline introduced in v1.3 and retained by v1.5.**

The main workflow is [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml).

## What the workflow validates

| Job | Platform/compiler | Configuration | oneTBB |
|---|---|---:|---:|
| `windows-msvc-release-tbb` | Windows/MSVC | Release | enabled and required |
| `linux-gcc-debug-no-tbb` | Linux/GCC | Debug | disabled |
| `linux-gcc-release-tbb` | Linux/GCC | Release | enabled and required |
| `linux-clang-release-tbb` | Linux/Clang | Release | enabled and required |
| `macos-appleclang-release-tbb` | macOS/Apple Clang | Release | enabled and required |
| `linux-clang-debug-asan-ubsan` | Linux/Clang | Debug + ASan/UBSan, Native vision enabled | disabled |
| `linux-clang-release-native-vision` | Linux/Clang | Release, Native-only v1.5 vision + installed consumer | disabled |
| `documentation-and-release-evidence` | Linux/Python | Markdown links, versions, accepted v1.5 assets, ZIP evidence, tool syntax | not applicable |

Each normal platform matrix job:

1. configures SmartParallel with benchmarks disabled;
2. builds the core library and deterministic validation suite;
3. runs CTest, including native hardware-topology validation;
4. installs SmartParallel;
5. configures a separate external core consumer with `find_package`;
6. builds and runs that consumer, including an installed-package hardware-discovery check.

The v1.5 Native-vision job additionally builds `SmartParallel::vision` without OpenCV, runs the v1.5 route/SIMD tests, installs the separate `SmartParallelVision` package, and runs the external vision consumer. The sanitizer job now includes the Native vision module. The documentation job validates local links, release-version consistency, accepted benchmark metrics, evidence hashes/ZIP contents, and Python publication-tool syntax.

The workflow runs for pushes to `main`, pull requests targeting `main`, and manual `workflow_dispatch`. Concurrency cancellation stops an older run for the same branch when a newer commit arrives.

## Final v1.3 workflow result

The final pull-request run passed all six jobs listed above. The normal platform jobs each passed the 16 deterministic CTest tests, installation, and the external package consumer. The sanitizer job passed all 16 tests under AddressSanitizer and UndefinedBehaviorSanitizer.

A green pull-request result means the exact commit under review compiled and passed the configured correctness gates. After merging, verify that the automatically triggered `main` workflow also finishes green before creating the v1.3 tag or GitHub Release.

## Enable and use it in the current GitHub interface

### First run

1. Push `.github/workflows/ci.yml` to a branch in the repository.
2. Open a pull request with **base: `main`** and **compare: your working branch**. The pull-request trigger starts CI even when ordinary pushes to the working branch are not configured as push triggers.
3. Open the pull request and view its **Checks** area, or open the repository's **Actions** tab and select **CI**.
4. Wait for all current jobs to finish. Do not merge while a job is queued, running, cancelled without replacement, or red.
5. Open any failed job, expand the first failed step, and use its compiler/CMake/CTest output as the source of truth.

### Manual run

The workflow contains `workflow_dispatch`. After the workflow file exists on the default branch:

1. Open **Actions**.
2. Select **CI** in the left sidebar.
3. Click **Run workflow**.
4. Choose the branch and confirm **Run workflow**.

### Actions permissions

When GitHub reports that Actions is disabled or restricted:

1. Open **Settings** for the repository.
2. In the left sidebar, open **Actions → General**.
3. Choose an Actions policy that permits `actions/checkout` and `actions/cache`.
4. Save the setting and rerun the workflow.

No repository secret is required by the default workflow.

### Protect `main` after the first successful run

1. Open **Settings → Rules → Rulesets**.
2. Create a branch ruleset targeting the default branch or `main`.
3. Enable **Require a pull request before merging**.
4. Enable **Require status checks to pass** and add the current platform, sanitizer, Native-vision, and documentation job names after they have appeared successfully in the repository.
5. Keep the ruleset active.

A check normally must have run successfully in the repository before GitHub offers it as a required check.

## Why vcpkg cannot be permanently installed on hosted runners

GitHub-hosted runners are newly provisioned for jobs and discarded afterward. Files installed during one run are not guaranteed to exist in the next run.

The workflow avoids repeated expensive dependency builds in two ways:

- It uses the runner-provided vcpkg installation when available.
- It stores compiled vcpkg binary packages with `actions/cache` under `.cache/vcpkg-binaries`.
- On a runner without vcpkg, it creates a fallback checkout and caches that checkout under `.cache/vcpkg-source`.

The first run for an operating-system/compiler combination may build oneTBB. Later runs restore the binary package cache when `vcpkg.json` has not changed.

## Truly permanent vcpkg installation

A permanent installation requires self-hosted runners. Provide one maintained machine for each operating system you want to own:

- Windows runner with MSVC and vcpkg, for example `C:\Tools\vcpkg`.
- Linux runner with GCC, Clang, Ninja, CMake, and `/opt/vcpkg`.
- macOS runner with Xcode command-line tools, CMake, Ninja, and `/opt/vcpkg`.

Set `VCPKG_ROOT` as a machine-level environment variable, register each runner with descriptive labels, and change the workflow's `runs-on` values to those labels. Self-hosted runners must be patched, secured, cleaned, and monitored by the repository owner.

For most public projects, GitHub-hosted runners plus binary caching are simpler and safer than maintaining three permanent machines.

## Optional organization-wide binary cache

Large organizations can replace the repository cache with a vcpkg binary cache hosted in GitHub Packages or another NuGet feed. That requires package read/write permissions and authentication. The repository workflow intentionally uses `actions/cache` so that a normal fork or public repository works without package-feed credentials.

## Cache maintenance

- Changing `vcpkg.json` creates a new binary-cache key.
- View or delete repository caches from **Actions → Management → Caches** when diagnosing a dependency issue.
- The fallback vcpkg source cache uses a dated key. Update that key periodically when a newer vcpkg tool is required by a new compiler image.
- The manifest's `builtin-baseline` remains the source of dependency-version reproducibility.
