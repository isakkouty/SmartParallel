# Beta 1.0 Release Checklist

## Required before publishing

- [ ] Perform a clean clone and Release build
- [ ] Run all benchmark executables under consistent conditions
- [ ] Regenerate all figures from the committed CSV files
- [ ] Verify every Markdown link and image path on GitHub
- [ ] Remove executables, build folders, and Python caches
- [ ] Tag the release consistently (`beta-1.0` or `v1.0.0-beta.1`)

## Strongly recommended

- [ ] Add CI for MSVC Release builds
- [ ] Add unit tests for workload mapping and plan fallback
- [ ] Test on a second machine
- [ ] Record benchmark hardware, compiler version, and power settings
- [ ] Decide whether raw benchmark CSVs are release artifacts or repository data
