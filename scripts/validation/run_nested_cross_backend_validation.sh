#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
repetitions="${1:-31}"

./scripts/validation/run_nested_release_validation.sh "$repetitions"
./scripts/validation/run_nested_release_validation.sh 3 trace thread_pool reuse
./scripts/validation/run_nested_release_validation.sh "$repetitions" static reuse
./scripts/validation/run_nested_release_validation.sh 3 trace static reuse
./scripts/validation/run_nested_release_validation.sh "$repetitions" tbb
./scripts/validation/run_nested_release_validation.sh 3 trace tbb reuse
python3 scripts/validation/compare_nested_backend_results.py validation/output
