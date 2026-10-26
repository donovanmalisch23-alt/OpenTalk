# Status and verification report

**Last updated:** 2026-10-26, Phase 0 complete and Phase 1 mostly complete.

## What exists

| Area | State | Evidence |
|---|---|---|
| Build system (CMake 3.21+, C++20, presets for Windows MSVC, Linux, ASan, fuzz) | Built on Linux | Clean build with `-Werror` on GCC 13.3 and Clang 18.1, Qt 6.4.2 |
| Logging library | Unit-tested | `tst_logging` |
| Data layout and portable mode | Unit-tested | `tst_apppaths` |
| Settings schema and store (validation, atomic save, corruption recovery, migration, newer-version protection) | Unit-tested | `tst_settingsstore` (12 cases) |
| Protocol framing and version negotiation | Unit-tested and fuzzed | `tst_frame`, `tst_version`; libFuzzer ran 642,521 inputs in 60 seconds with no failures |
| Main window and settings dialog | Implemented, automated accessibility checks | `tst_mainwindow`: accessible names on every focusable control, pane shortcuts, F6 order, settings navigation, apply and save |
| Sanitizers | All tests pass under AddressSanitizer and UBSan | `linux-asan` preset |
| Windows build | **Not yet verified** | The CI job is written; the result of its first run is reported on the pull request or commit |
| NVDA behaviour | **Not yet verified** | Needs a manual run of the [test script](accessibility/nvda-test-script.md) on Windows |
| Live announcements (`QAccessibleAnnouncementEvent`) | Implemented, **compiled only on Qt 6.8 (Windows CI)** | Not testable on the local Qt 6.4 |

## Commands that were run (Linux sandbox, Ubuntu 24.04)

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug   # 6/6 passed
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan    # 6/6 passed
cmake --preset linux-fuzz  && cmake --build --preset linux-fuzz
build/linux-fuzz/tests/fuzz/fuzz_frame_decoder -max_total_time=60 corpus                          # no crashes
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror               # clean
```

The accessible-name test was also checked against a deliberate fault: removing one accessible name made it fail as expected.

## Not done yet in Phase 1

- No application icon or visual identity yet (M1.7).
- No screen-reader-verified behaviour (M1.6).
- Translations are not set up, although all user-visible strings use `tr()`.

## Decisions needed from the project owner

| # | Decision | Recommendation |
|---|---|---|
| 1 | Minimum Windows version | **Windows 11 x64** supported. Windows 10 22H2 best-effort, since its consumer security updates ended in October 2026. |
| 2 | Approve the architecture, including the separate server process ([ADR 0004](architecture/decisions/0004-process-model.md)) | Approve |
| 3 | Server identity model | Trust on first use with pinning by default, plus normal CA certificates |
| 4 | Contributions | Developer Certificate of Origin (sign-off), no CLA |
| 5 | Code signing for releases (costs money; reduces SmartScreen warnings) | Decide before Phase 8 |
| 6 | Virtual audio driver | Do not build one. Use WASAPI loopback, and revisit after Phase 7 research |
| 7 | Default branch | Use `main` for the first merge of this work |
