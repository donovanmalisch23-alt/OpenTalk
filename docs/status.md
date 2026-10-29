# Status and verification report

**Last updated:** 2026-10-29. Phases 0 and 2 to 8 are complete. Still open: the manual NVDA pass, the application icon, checks of Phases 4 and 7 on real sound hardware, legal review of the plugin licensing policy, and owner decisions on code signing and a virtual audio driver.

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

**Advanced audio (Phase 7):**

- Noise suppression (on by default), automatic gain and echo cancellation; voice effects (rumble filter, bass, treble, voice changer).
- Playing WAV, FLAC and MP3 files into a channel with pause, skip and stop, and sharing computer audio: OpenTalk's own sound is left out on Windows 11, and removed by echo cancellation with a warning on Windows 10.
- Listeners see "playing a file" or "sharing computer audio", never "talking", and can mute both without muting voices. A separate permission controls who may stream.
- SDK 0.4 lets bots play files; the example bot has `!play`.
- All of it is tested with synthetic sound devices and generated signals. **None of it has been tried on real sound hardware yet.**

**Hosting and packaging (Phase 8):**

- A per-user Windows installer next to the portable zip. Both carry OpenSSL 3.5.9 LTS and licence notices for everything bundled.
- "Remember my password" in Windows Credential Manager.
- Automatic daily server backups, with `--backup`, a verified `--restore` and `--list-backups`.
- A container image and a sandboxed systemd unit. `opentalk-ping --voice` checks reachability from outside, including UDP.
- Guides for hosting, getting started and troubleshooting.
- CI adds clang-tidy, CodeQL, fully instrumented fuzzing (six fuzzers, nightly runs) and a 150-client load test.

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
| SDK 0.4 and the example bot as a real program: bot marking, greeting, commands, audio, playing files | Integration-tested against a real server | `tst_examples` |
| Owner recovery, audit verification and export on the command line | Integration-tested against the real program | `tst_server_process` |
| TLS transport, hostile clients | Integration-tested | `tst_tls_session` (15) |
| Several clients chatting; reconnection after a server restart | Integration-tested | `tst_chat_session` (9); 10 runs in a row with no failures |
| Real `opentalk-server` and `opentalk-ping` programs | Integration-tested | `tst_server_process` |
| Hosting a server from the app | Integration-tested against the real program | `tst_local_server`; `tst_mainwindow_online` hosting test |
| Main window connected to a real server: trust prompt, tree, chat, private messages, announcements, accessible names | Integration-tested (offscreen) | `tst_mainwindow_online` (10) |
| Announcement spacing and coalescing | Unit-tested | `tst_announcements` |
| Audio engine: Opus, jitter buffer, mixing, voice activation, push-to-talk, mute, deafen, user volume, device recovery | Unit-tested with a synthetic sound device (tone detection) | `tst_audio_core` (17), `tst_audio_engine` (15 with Phase 7) |
| Noise suppression, automatic gain, echo cancellation (also while both people talk) | Unit-tested on generated speech, noise and echo | `tst_voice_processing` (9); quality and cost in the [benchmarks](audio/benchmarks.md) |
| Voice effects: filter responses, voice changer pitch | Unit-tested (measured gains and pitch) | `tst_voice_effects` (6) |
| Reading WAV, FLAC and MP3 files in pieces, resampling, seeking, refusing other files | Unit-tested | `tst_media_file` (5) |
| Streaming a file at real speed, pause, skip, stop, end; muting others' files; sharing computer audio with and without leaving OpenTalk out (own sound 32–43 dB lower in the fallback) | Unit-tested with synthetic devices, including a simulated Windows 10 loopback | `tst_audio_engine` |
| Files and computer audio in the app: menu, shortcuts, status, labels for listeners, announcements, muting, warning on Windows 10, stop on disconnect, guests refused | Integration-tested (offscreen, two windows and a real server) | `tst_media_ui` (7) |
| `media.transmit` enforced separately from voice, per channel; voice mutes silence streams too | Unit-tested | `tst_moderation_server` (`streamingFilesNeedsItsOwnPermission`) |
| The example bot plays files from its folder only, as media | Integration-tested against a real server | `tst_examples` |
| Files and computer audio with real devices on Windows 10 and 11 | **Not yet verified** | NVDA script steps 65–74 |
| Voice encryption, forwarding and fallback | Unit-tested, fuzzed and integration-tested over real sockets | `tst_packetcipher` (10), `tst_voicerouter` (10), `tst_voice_session` (5 tests: UDP, TCP fallback, a server without UDP, channel isolation, a tone travelling end to end); `fuzz_voice_router` (434,200 inputs locally, no failures) |
| Voice in the app: push-to-talk key, audio menu, settings pages, talking indicators, per-user volume | Unit- and integration-tested (offscreen) | `tst_pushtotalk` (10), `tst_mainwindow` (11), `tst_mainwindow_online` voice test |
| Audio performance | Measured on a VM without sound hardware | [Benchmarks](audio/benchmarks.md) |
| Remembered passwords: only after a successful login, filled in next time, forgotten when unticked or the server is deleted | Integration-tested (offscreen, real server); the real Credential Manager is tested on Windows CI | `tst_remember_password` (5), `tst_credentialstore` |
| Server backups: VACUUM INTO while running, integrity and version checks, schedule and retention, restore with a safety copy, refusal while a server runs, one server per data folder | Unit- and integration-tested against the real program | `tst_serverdatabase`, `tst_servercore` (`automaticBackupsFollowTheSchedule`), `tst_server_process` |
| Reachability check (`opentalk-ping --voice`) with and without UDP | Integration-tested against the real programs | `tst_server_process` (`pingChecksTheVoicePath`) |
| Per-address connection cap; 150 clients connecting, chatting and leaving at once | Integration-tested | `tst_tls_session`: 150 clients connected in about 1.5 s; a message reached all of them in about 50 ms |
| Windows installer: silent install, files, no portable marker, uninstall entry, uninstall that leaves other files | Tested in Windows CI | "Build and test the installer" step |
| Licence notices: every DLL in the package is covered, licence files present | Checked in Windows CI | Packaging step fails on an unlisted DLL; `THIRD-PARTY-NOTICES.txt`, `licenses/` |
| Packaged OpenSSL is from the 3.5 LTS series | Checked in Windows CI | Smoke test reads the server's "TLS library" line |
| Server container image: start, connect, automatic backup, graceful stop | Tested in CI and in the sandbox | "Server container image" job |
| Static analysis | clang-tidy clean on all sources ([rules](../.clang-tidy)); CodeQL security queries run in CI | CI "Static analysis" step, CodeQL workflow |
| Fuzzing with full instrumentation | 3 minutes per fuzzer locally (28 October 2026): frames 539,000 runs, messages 4.75 million, sessions 666,000, server core 77,000, voice router 523,000, audio files 58,000. No failures in OpenTalk's code. | Nightly workflow runs 10 minutes each. Before 28 October only the harnesses were instrumented, so older fuzzing figures say less than they seemed to. The audio-file fuzzer found undefined behaviour in miniaudio's FLAC seeking (recorded in the [third-party register](licensing/third-party-register.md)). |
| Voice with real microphones and speakers on Windows | **Not yet verified** | Needs `opentalk-audio-bench device-loopback` and a listening test |
| All tests under AddressSanitizer and UBSan | Passing (39 test programs) | `linux-asan` preset in CI. UBSan found a shutdown-order bug in the main window, now fixed |
| Portable Windows zip | Built and smoke-tested in CI | With only Windows on `PATH`, the smoke test generates a certificate, logs in, runs the Opus benchmark and sends voice through a server |
| NVDA behaviour | **Not yet verified** | [Test script](accessibility/nvda-test-script.md), now 74 steps |
| Ctrl+C shutdown of `opentalk-server` in a Windows console | **Not yet verified manually** | Shutdown when the app closes the server's input is tested on Windows CI |

## Known limitations

- There is no server-side message history (by design for now: messages are not stored).
- Moderation gaps: bans by address range, reporting a single message, and a mass-action safeguard ([details](security/moderation-model.md#not-yet-implemented)).
- Plugins are not sandboxed by the operating system (they run with the user's rights), and cannot play audio; bots built with the SDK can.
- Only WAV, FLAC and MP3 files can be played into a channel. On Windows 10, shared computer audio may still carry some of other people's voices; routing OpenTalk to another device avoids it. Echo cancellation cannot remove a screen reader's speech.
- Voice has not been tried on real sound hardware. Push-to-talk works globally only on Windows; elsewhere it works while OpenTalk's window is in front.
- Remembered passwords exist only on Windows (Credential Manager); elsewhere the app asks on each connect.
- Releases are not code-signed yet, so Windows SmartScreen warns about them ([options](development/release-signing.md)).
- The Windows server's private key relies on the user's data folder permissions; no extra access-control list is set yet.

## Commands that were run (Linux sandbox, Ubuntu 24.04)

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug   # 39/39 pass
run-clang-tidy -p build/linux-debug -quiet "$PWD/src/.*\.cpp$"                              # no findings
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan    # 39/39 pass
cmake --preset linux-fuzz  && cmake --build --preset linux-fuzz                                 # 6 fuzzers
docker build -t opentalk-server . && docker run ... && opentalk-ping ...                        # container works
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

1. Run the [NVDA test script](accessibility/nvda-test-script.md) on the portable zip. Steps 19–30 cover hosting, joining and chatting; steps 33–43 cover voice; steps 46–57 cover moderation; steps 58–64 cover plugins; steps 65–74 cover voice processing, files and computer audio.
2. With headphones (or a cable from output to input), try voice with a second person and run `opentalk-audio-bench device-loopback`; please record the result in an issue.
3. Switch the repository's default branch to `main` (Settings → General → Default branch).
4. Paste the repository description and enable private vulnerability reporting (Settings → Security).
5. Have the [plugin licensing policy](plugins/licensing.md) reviewed by a lawyer before the first public release. It decides whether third-party plugins may use any licence.
6. Decide on a virtual audio driver using the [feasibility report](audio/virtual-driver-feasibility.md). The recommendation is not to build one now; you approved "no virtual audio driver for now" on 2026-10-26, and the report confirms the costs.
7. Try steps 65–74 of the NVDA script (files and computer audio), on Windows 11 and, if possible, Windows 10.
8. Decide on code signing ([options and costs](development/release-signing.md)). Until then releases are unsigned, with SHA-256 sums and GitHub build provenance.
9. Optionally report the miniaudio FLAC finding upstream (details in the third-party register).
