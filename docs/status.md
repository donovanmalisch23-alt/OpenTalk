# Status and verification report

**Last updated:** 2026-10-28. Phases 0, 2, 3, 4, 5 and 6 are complete. Still open: the manual NVDA pass, the application icon, checks of Phase 4 on real sound hardware, and legal review of the plugin licensing policy.

## What works today

People can host a server from the app or run `opentalk-server` on its own. Then they can log in with an account or as a guest, see channels and who is in them, join, create and delete channels (by role), and send channel, private and server-wide text messages. Everything goes over TLS 1.3 with certificate pinning.

**Voice (Phase 4):**

- Everyone in a channel can talk, with push-to-talk (a global key on Windows), voice activation or always-on.
- Microphone mute, mute all, per-person volume, and talking indicators in the channel list.
- Event sounds and soundpacks.
- Voice is encrypted over UDP and falls back to the main connection when UDP is blocked.
- It is tested end to end with synthetic sound devices. **It has not yet been tried with real sound hardware.**

**Moderation and administration (Phase 5):**

- Roles with ranks (owner, administrator, moderator, registered user, guest, restricted, and custom roles), capabilities, and per-channel rules for roles or single accounts, which also make channel moderators.
- Warnings, mutes (voice, text or both), kicks, bans (optionally by address), restrictions and moves, server-wide or in one channel, with required reasons and automatic expiry.
- Reports, appeals decided by someone other than the issuing moderator, a hash-chained audit log with verification, export and retention, owner recovery, server settings and statistics.
- In the app: a Moderation menu and context actions offered by capability, role and mute labels in the channel list, clear messages to the person sanctioned, and an Administration dialog with nine tabs.

**Plugins and bots (Phase 6):**

- Plugins are separate programs in any language, talking JSON lines with the app. The user turns each on in a consent dialog listing what it may do, and must review it again whenever it changes. The app enforces those permissions, limits what plugins send, and turns off plugins that misbehave or keep crashing.
- A plugin manager (install from a folder, turn on and off, restart, read the log, remove), a Plugins menu with the plugins' own commands, and `--no-plugins` for troubleshooting.
- Examples: a dice roller in C++ and a "who is here" plugin in Python.
- Bot accounts (`--bot`) that everyone sees marked as bots, SDK 0.3 with audio, and an example greeter bot that greets, answers commands and plays a tone.

| Area | State | Evidence |
|---|---|---|
| Build (CMake, C++20; MSVC, GCC and Clang with warnings as errors) | Built on Windows and Linux CI | Windows: MSVC 14.44, Qt 6.8.3, vcpkg OpenSSL 3.6.5 and libsodium 1.0.22. Linux: GCC 13.3 and Clang 18.1, Qt 6.4.2 |
| Settings, data folders, saved servers | Unit-tested | `tst_settingsstore`, `tst_apppaths`, `tst_serverprofiles` |
| Protocol codec, framing, versions, session rules | Unit-tested and fuzzed | `tst_messages` (126 cases, including every moderation message), `tst_frame`, `tst_version`, `tst_session` (22); fuzzers for frames, messages and sessions |
| Server rules: login, roles, channels, text, flood control | Unit-tested and fuzzed | `tst_servercore` (21); `fuzz_server_core` (311,308 inputs locally, no failures) |
| Argon2id hashing, certificate generation, server database (schema 2, upgrade from Phase 3) | Unit-tested on Linux and Windows | `tst_passwordhash`, `tst_certificate`, `tst_serverdatabase` (8) |
| Moderation rules: resolution, ranks, sanctions, audit chain, appeals | Unit-tested | `tst_moderation` (14) |
| Every request against every role | Unit-tested | `tst_authorization` (182 cases) |
| Moderation through the server: sanctions on login, join, talk and text, expiry, reports, appeals, roles, rules, settings, ownership, audit retention, bot accounts | Unit-tested and fuzzed | `tst_moderation_server` (19); after Phase 5, `fuzz_server_core` ran 113,860 inputs and `fuzz_message_decoder` 3.39 million in a minute each, with no failures |
| Moderation in the app: menus by capability, warn, mute, kick, ban, reports, appeals, Administration dialog, accessible names | Integration-tested (offscreen, real windows and server) | `tst_moderation_ui` (9) |
| Plugin manifests, installation, consent records, updates needing review | Unit-tested | `tst_plugin_manifest` (22) |
| Plugins as real processes: both directions of messages, menu commands, enforcement of permissions, floods, oversized lines, garbage, hangs and crash loops; the C++ and Python examples | Integration-tested against a real server | `tst_plugins` (10) |
| Plugins in the app: install, consent, Plugins menu, log, turn off, remove, review after an update, accessible names | Integration-tested (offscreen) | `tst_plugin_ui` (2) |
| SDK 0.3 and the example bot as a real program: bot marking, greeting, commands, audio | Integration-tested against a real server | `tst_examples` |
| Owner recovery, audit verification and export on the command line | Integration-tested against the real program | `tst_server_process` |
| TLS transport, hostile clients | Integration-tested | `tst_tls_session` (15) |
| Several clients chatting; reconnection after a server restart | Integration-tested | `tst_chat_session` (9); 10 runs in a row with no failures |
| Real `opentalk-server` and `opentalk-ping` programs | Integration-tested | `tst_server_process` |
| Hosting a server from the app | Integration-tested against the real program | `tst_local_server`; `tst_mainwindow_online` hosting test |
| Main window connected to a real server: trust prompt, tree, chat, private messages, announcements, accessible names | Integration-tested (offscreen) | `tst_mainwindow_online` (10) |
| Announcement spacing and coalescing | Unit-tested | `tst_announcements` |
| Audio engine: Opus, jitter buffer, mixing, voice activation, push-to-talk, mute, deafen, user volume, device recovery | Unit-tested with a synthetic sound device (tone detection) | `tst_audio_core` (17), `tst_audio_engine` (9) |
| Voice encryption, forwarding and fallback | Unit-tested, fuzzed and integration-tested over real sockets | `tst_packetcipher` (10), `tst_voicerouter` (10), `tst_voice_session` (5 tests: UDP, TCP fallback, a server without UDP, channel isolation, a tone travelling end to end); `fuzz_voice_router` (434,200 inputs locally, no failures) |
| Voice in the app: push-to-talk key, audio menu, settings pages, talking indicators, per-user volume | Unit- and integration-tested (offscreen) | `tst_pushtotalk` (10), `tst_mainwindow` (11), `tst_mainwindow_online` voice test |
| Audio performance | Measured on a VM without sound hardware | [Benchmarks](audio/benchmarks.md) |
| Voice with real microphones and speakers on Windows | **Not yet verified** | Needs `opentalk-audio-bench device-loopback` and a listening test |
| All tests under AddressSanitizer and UBSan | Passing (33 test programs) | `linux-asan` preset in CI. UBSan found a shutdown-order bug in the main window, now fixed |
| Portable Windows zip | Built and smoke-tested in CI | With only Windows on `PATH`, the smoke test generates a certificate, logs in, runs the Opus benchmark and sends voice through a server |
| NVDA behaviour | **Not yet verified** | [Test script](accessibility/nvda-test-script.md), now 45 steps |
| Ctrl+C shutdown of `opentalk-server` in a Windows console | **Not yet verified manually** | Shutdown when the app closes the server's input is tested on Windows CI |

## Known limitations

- There is no server-side message history (by design for now: messages are not stored).
- Moderation gaps: bans by address range, reporting a single message, and a mass-action safeguard ([details](security/moderation-model.md#not-yet-implemented)).
- Plugins are not sandboxed by the operating system (they run with the user's rights), and cannot yet play audio (planned with media streaming in Phase 7).
- Voice has not been tried on real sound hardware. Push-to-talk works globally only on Windows; elsewhere it works while OpenTalk's window is in front.
- Passwords are not remembered; the app asks on each connect.
- The packaged OpenSSL (3.6.5) series ends support around November 2026. A supported LTS is required before a public release ([ADR 0007](architecture/decisions/0007-openssl-tls-backend.md)).
- The Windows server's private key relies on the user's data folder permissions; no extra access-control list is set yet.

## Commands that were run (Linux sandbox, Ubuntu 24.04)

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug   # 33/33 pass
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan    # 33/33 pass
cmake --preset linux-fuzz  && cmake --build --preset linux-fuzz                                 # 5 fuzzers
build/linux-fuzz/tests/fuzz/fuzz_voice_router -max_total_time=60 corpus/                        # no failures
build/linux-release/tools/audio-bench/opentalk-audio-bench all                                  # docs/audio/benchmarks.md
for i in $(seq 10); do build/linux-debug/tests/integration/tst_chat_session; done               # 0 failures
```

Windows results come from the CI runs linked on the pull request.

## Recorded owner decisions

| Date | Decision |
|---|---|
| 2026-10-26 | Minimum Windows 10 (1809+) and Windows 11, which led to [ADR 0007](architecture/decisions/0007-openssl-tls-backend.md). Architecture approved, including the separate server process. Trust on first use approved. DCO for contributions. No virtual audio driver for now. `main` created. Code signing to be decided before Phase 8. |

## Open items for the owner

1. Run the [NVDA test script](accessibility/nvda-test-script.md) on the portable zip. Steps 19–30 cover hosting, joining and chatting; steps 33–43 cover voice; steps 46–57 cover moderation; steps 58–64 cover plugins.
2. With headphones (or a cable from output to input), try voice with a second person and run `opentalk-audio-bench device-loopback`; please record the result in an issue.
3. Switch the repository's default branch to `main` (Settings → General → Default branch).
4. Paste the repository description and enable private vulnerability reporting (Settings → Security).
5. Have the [plugin licensing policy](plugins/licensing.md) reviewed by a lawyer before the first public release. It decides whether third-party plugins may use any licence.
