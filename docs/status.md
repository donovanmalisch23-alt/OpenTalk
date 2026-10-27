# Status and verification report

**Last updated:** 2026-10-27. Phases 0 and 2 are complete. Phase 1 is complete except the manual NVDA pass and the application icon.

## What exists

| Area | State | Evidence |
|---|---|---|
| Build system (CMake, C++20, presets for Windows MSVC, Linux, ASan, fuzz) | Built on Windows and Linux CI | MSVC 14.44 with Qt 6.8.3; GCC 13.3 and Clang 18.1 with Qt 6.4.2; warnings are errors everywhere |
| Logging, data layout, settings store | Unit-tested | `tst_logging`, `tst_apppaths`, `tst_settingsstore` |
| Main window and settings dialog | Automated accessibility checks | `tst_mainwindow` |
| Protocol: framing, CBOR session messages, version negotiation | Unit-tested and fuzzed | `tst_frame`, `tst_messages` (34 cases), `tst_version`; fuzzers `fuzz_frame_decoder`, `fuzz_message_decoder` |
| Protocol: client and server session state machines (handshake, heartbeat, timeouts, rate limit, goodbye) | Unit-tested and fuzzed | `tst_session` (22 cases with a fake clock); `fuzz_session` attacks both sides |
| TLS 1.3 with the OpenSSL backend, fingerprints, certificate pinning | Integration-tested on Linux and Windows | `tst_tls_session` (15 cases, including hostile clients); passed 20 runs in a row locally |
| Server library and headless `opentalk-server` | Integration-tested as a real process | `tst_server_process` starts the real server and `opentalk-ping` (Linux and Windows CI); Ctrl+C and SIGTERM shutdown verified on Linux |
| SDK 0.1 (`opentalk::sdk::Client`) and `opentalk-ping` | Tested through the example | `tst_server_process` |
| All tests under AddressSanitizer and UBSan | Passing | `linux-asan` preset in CI |
| Portable Windows zip (app, server, ping tool, Qt, OpenSSL, VC++ runtime) | Built in CI with a self-containment smoke test | The CI "Smoke-test the portable package" step runs the packaged server and ping tool with only Windows on `PATH` |
| NVDA behaviour | **Not yet verified** | Needs a manual run of the [test script](accessibility/nvda-test-script.md) |
| Graceful Ctrl+C shutdown of `opentalk-server` on Windows | **Not yet verified** | Implemented with `SetConsoleCtrlHandler`; needs a manual check |
| Desktop app connecting to a server | **Not implemented** | Phase 3 |

## Fuzzing totals from this phase (local, Linux)

| Fuzzer | Inputs | Result |
|---|---|---|
| `fuzz_frame_decoder` | 642,521 | No failures |
| `fuzz_message_decoder` | 5,010,163 | No failures |
| `fuzz_session` (server side, then both sides) | 346,493 + 373,384 | No failures |

CI also runs every fuzzer for one minute on each push.

## Commands that were run (Linux sandbox, Ubuntu 24.04)

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug   # 10/10 test programs pass
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan    # 10/10 pass
for i in $(seq 20); do build/linux-debug/tests/integration/tst_tls_session; done                  # 0 failures in 20 runs
build/linux-debug/src/server/opentalk-server --tls-cert tests/data/test-server.crt \
    --tls-key tests/data/test-server.key --port 0 &                                               # manual smoke test
build/linux-debug/examples/opentalk-ping/opentalk-ping 127.0.0.1 --port <port> --fingerprint <fp> # connected, round trip measured
```

## Recorded owner decisions (2026-10-26)

| Decision | Outcome |
|---|---|
| Minimum Windows version | **Windows 10** (1809 or later) and Windows 11. This led to [ADR 0007](architecture/decisions/0007-openssl-tls-backend.md): OpenSSL is shipped, because Windows 10 has no built-in TLS 1.3. |
| Architecture, including the separate server process | Approved |
| Server identity | Trust on first use with pinning, plus normal CA certificates: approved |
| Contributions | Developer Certificate of Origin, no CLA: approved |
| Code signing | To be decided before Phase 8 |
| Virtual audio driver | Not built; WASAPI loopback first |
| Default branch | `main` created from the foundation commit |

## Open items for the owner

1. Run the [NVDA test script](accessibility/nvda-test-script.md) on the portable zip.
2. Switch the repository's default branch to `main` (Settings → General → Default branch). Tools available to Claude cannot change this setting.
3. Paste the repository description and enable private vulnerability reporting (Settings → Security).
