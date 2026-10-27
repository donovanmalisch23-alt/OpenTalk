# Status and verification report

**Last updated:** 2026-10-27. Phases 0, 2 and 3 are complete. Phase 1 is complete except the manual NVDA pass and the application icon.

## What works today

People can host a server from the app or run `opentalk-server` on its own. Then they can log in with an account or as a guest, see channels and who is in them, join, create and delete channels (by role), and send channel, private and server-wide text messages. Everything goes over TLS 1.3 with certificate pinning. **There is no voice yet** (Phase 4).

| Area | State | Evidence |
|---|---|---|
| Build (CMake, C++20; MSVC, GCC and Clang with warnings as errors) | Built on Windows and Linux CI | Windows: MSVC 14.44, Qt 6.8.3, vcpkg OpenSSL 3.6.5 and libsodium 1.0.22. Linux: GCC 13.3 and Clang 18.1, Qt 6.4.2 |
| Settings, data folders, saved servers | Unit-tested | `tst_settingsstore`, `tst_apppaths`, `tst_serverprofiles` |
| Protocol codec, framing, versions, session rules | Unit-tested and fuzzed | `tst_messages` (76 cases), `tst_frame`, `tst_version`, `tst_session` (22); fuzzers for frames, messages and sessions |
| Server rules: login, roles, channels, text, flood control | Unit-tested and fuzzed | `tst_servercore` (21); `fuzz_server_core` (311,308 inputs locally, no failures) |
| Argon2id hashing, certificate generation, server database | Unit-tested on Linux and Windows | `tst_passwordhash`, `tst_certificate`, `tst_serverdatabase` |
| TLS transport, hostile clients | Integration-tested | `tst_tls_session` (15) |
| Several clients chatting; reconnection after a server restart | Integration-tested | `tst_chat_session` (9); 10 runs in a row with no failures |
| Real `opentalk-server` and `opentalk-ping` programs | Integration-tested | `tst_server_process` |
| Hosting a server from the app | Integration-tested against the real program | `tst_local_server`; `tst_mainwindow_online` hosting test |
| Main window connected to a real server: trust prompt, tree, chat, private messages, announcements, accessible names | Integration-tested (offscreen) | `tst_mainwindow_online` (10) |
| Announcement spacing and coalescing | Unit-tested | `tst_announcements` |
| All tests under AddressSanitizer and UBSan | Passing (19 test programs) | `linux-asan` preset in CI |
| Portable Windows zip | Built and smoke-tested in CI | The smoke test generates a certificate and logs in with only Windows on `PATH` |
| NVDA behaviour | **Not yet verified** | [Test script](accessibility/nvda-test-script.md), now 32 steps |
| Ctrl+C shutdown of `opentalk-server` in a Windows console | **Not yet verified manually** | Shutdown when the app closes the server's input is tested on Windows CI |

## Known limitations

- No voice, sounds, moderation beyond the three fixed roles, or server-side message history (by design for now: messages are not stored).
- Passwords are not remembered; the app asks on each connect.
- The packaged OpenSSL (3.6.5) series ends support around November 2026. A supported LTS is required before a public release ([ADR 0007](architecture/decisions/0007-openssl-tls-backend.md)).
- The Windows server's private key relies on the user's data folder permissions; no extra access-control list is set yet.

## Commands that were run (Linux sandbox, Ubuntu 24.04)

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug   # 19/19 pass
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan    # 19/19 pass
cmake --preset linux-fuzz  && cmake --build --preset linux-fuzz                                 # 4 fuzzers
for i in $(seq 10); do build/linux-debug/tests/integration/tst_chat_session; done               # 0 failures
```

Windows results come from the CI runs linked on the pull request.

## Recorded owner decisions

| Date | Decision |
|---|---|
| 2026-10-26 | Minimum Windows 10 (1809+) and Windows 11, which led to [ADR 0007](architecture/decisions/0007-openssl-tls-backend.md). Architecture approved, including the separate server process. Trust on first use approved. DCO for contributions. No virtual audio driver for now. `main` created. Code signing to be decided before Phase 8. |

## Open items for the owner

1. Run the [NVDA test script](accessibility/nvda-test-script.md) on the portable zip. Steps 19–30 cover hosting, joining and chatting.
2. Switch the repository's default branch to `main` (Settings → General → Default branch).
3. Paste the repository description and enable private vulnerability reporting (Settings → Security).
