# Changelog

All notable changes are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

Nothing yet.

## [1.0.0-rc.1] - 2026-10-29

The first published version: a release candidate for 1.0.0, with everything built in Phases 0 to 9. It has not yet been tried with NVDA or with real sound hardware; see the [release notes](docs/release-notes/1.0.0-rc.1.md).

### Added

- Status messages: Server, "Set my status message" (Ctrl+Shift+S) shows a short note after your name for everyone (protocol `SetStatus`, `UserUpdated` and user field 6; SDK `Client::setStatus()` and `User::status`). Refused while text-muted; shares the chat flood limit.
- Help, "Create a support file": shows, then saves or copies, the version, system, sound devices, plugins, settings and end of the log, leaving out passwords, messages, server addresses, other people's names and personal folders.
- File, "Export settings" and "Import settings", for moving settings to another computer. The previous settings are kept before an import.
- Audio, "Share one program's sound": sends only the chosen program's sound (Windows 11), so the screen reader and notifications stay private.
- SDK 0.5: `createChannel()`, `deleteChannel()`, `moveUser()`, `applySanction()` and `liftSanction()`, with `requestResult()` giving the new channel's or sanction's ID; tested against a real server (`tst_sdk`).
- Administration, Statistics also shows data received and sent, failed logins and connections ended by errors (protocol: optional statistics fields 13–16).
- An application icon, OpenTalk's own, on the window, the program and the installer; the program file now carries version details.
- Only one copy of OpenTalk can use a data folder at a time; starting a second one explains how to switch or use `--data-dir`. After a crash, the next start says so in the history and how to report it.
- Windows installer (per user, no administrator rights), next to the portable zip. Uninstalling keeps your settings.
- "Remember my password" in the connect dialog, kept in Windows Credential Manager only, and only after the server accepted it.
- Help, Third-party notices; the package now contains THIRD-PARTY-NOTICES.txt and every bundled licence. About shows the Qt and OpenSSL versions.
- Server: automatic daily database backups (`--backup-every-hours`, `--keep-backups`, `--backup-dir`), `--backup`, `--restore` and `--list-backups`; a second server can no longer use the same data folder.
- `opentalk-ping --voice` checks that voice gets through on UDP (exit code 3 when only the fallback works).
- SDK: `Client::isAudioUsingUdp()` and `audioPathChanged()`.
- A container image (`Dockerfile`), a systemd unit, and guides for hosting, getting started and troubleshooting.
- CI: clang-tidy, CodeQL, nightly fuzzing, an audio-file fuzzer, and a 150-client load test.
- Voice processing (Phase 7): noise suppression (on by default), automatic gain and echo cancellation for speakers, using speexdsp 1.2.1 (added to the third-party register).
- Voice effects: rumble filter, bass, treble and a voice changer (pitch), on a new "Voice Effects and Media" settings page.
- Playing WAV, FLAC and MP3 files into your channel (Ctrl+Shift+F), with pause (Ctrl+Shift+K), back and forward 10 seconds (Ctrl+Shift+J, Ctrl+Shift+L), stop (Ctrl+Shift+X) and "what is playing" (Ctrl+Shift+I). The sender hears it too unless they turn that off.
- Sharing computer audio (Ctrl+Shift+C, Windows). On Windows 11 OpenTalk's own sound is left out; on Windows 10 it is removed by echo cancellation and the user is warned.
- Listeners see "playing a file" or "sharing computer audio" instead of "talking", can have it announced, and can mute files and computer audio from others without muting voices.
- The "Stream media files and computer audio" permission (`media.transmit`) is enforced separately from voice; guests do not have it by default.
- SDK 0.4: `AudioFileReader` for bots that play files. The example bot plays files from `--media-folder` with `!files`, `!play` and `!stop`.
- `opentalk-audio-bench processing` measures the cost of voice processing and effects.
- ADR 0010 and a virtual audio driver feasibility report (for an owner decision).
- Plugins (Phase 6): separate programs in any language that talk JSON lines with the app. A consent dialog lists what each may do; permissions are enforced, must be reviewed again when a plugin changes, and misbehaving or crash-looping plugins are turned off. Plugin manager, Plugins menu (Alt+P) and `--no-plugins`.
- Example plugins: a dice roller in C++ and a "who is here" plugin in Python, included in the portable zip under `examples\plugins`.
- Bot accounts: `opentalk-server --create-account <name> --bot`, or a check box in Administration. Everyone sees them marked as bots, and the audit log names them as bots. Server database schema 3 (upgraded automatically).
- SDK 0.3: users' role names, mutes and bot status; server-wide messages; sending and receiving audio with `AudioEncoder` and `AudioDecoder`. Example `opentalk-greeter-bot`, included in the portable zip.
- Round-trip tests for every moderation and administration message.
- Moderation and administration (Phase 5): roles with ranks (owner, administrator, moderator, registered user, guest, restricted, and custom roles), capabilities, and per-channel permission rules for roles or single accounts.
- Sanctions: warnings, voice and text mutes, kicks, bans (optionally by IP address), restrictions and moves, server-wide or in one channel, with required reasons, automatic expiry, and a clear message to the person sanctioned.
- Reports, appeals (decided by someone other than the issuing moderator), and a hash-chained audit log with `--verify-audit`, `--export-audit` and `--audit-retention-days`.
- Owner recovery (`--recover-owner`); `--role owner`, `moderator` and `restricted` for `--create-account`. The person hosting from the app becomes the owner.
- Desktop app: Moderation menu (Alt+O) and context actions shown only to those who may use them; role and mute labels in the channel list; Edit channel; Send a message to everyone; My sanctions and appeals; an Administration dialog (Ctrl+Shift+A) with sanctions, reports, appeals, accounts, roles, channel permissions, audit log, server settings and statistics.
- Server settings (name, welcome message, guests, user limit, voice quality, longest temporary ban, contact) changed from the app and kept across restarts.
- Server database schema 2; Phase 3 and 4 databases are upgraded automatically (a backup is kept) and the earliest administrator becomes the owner.
- Voice: push-to-talk (a global key on Windows), voice activation or always-on; microphone mute (Ctrl+M), mute all (Ctrl+Shift+M), talk toggle (Ctrl+T), per-person volume (Ctrl+Shift+V); talking indicators that do not disturb the screen reader; Audio and Sounds settings pages.
- Voice transport: Opus with forward error correction and an adaptive jitter buffer, sent over UDP encrypted with XChaCha20-Poly1305 (keys sent over TLS), with automatic fallback to the TLS connection when UDP is blocked. The server forwards voice to the speaker's channel without decoding it.
- Event sounds generated by OpenTalk itself, and soundpacks (WAV, FLAC or MP3 files in a folder).
- `opentalk-server --no-udp`, and the server prints its voice port.
- `opentalk-audio-bench`: codec, jitter, server and network measurements, plus a sound-hardware round-trip test. Results are in docs/audio/benchmarks.md.
- miniaudio 0.11.25 (vendored) and Opus added to the third-party register.

### Changed

- Windows packages use OpenSSL 3.5.9 (long-term support until 2030) instead of 3.6.

### Fixed

- Server: when an account logged in again while still online somewhere else, the server could read freed memory and crash, or log someone in under a wrong session number. Found by an intermittent Windows CI failure; the server's session table no longer moves entries when one is removed, and a new test logs one account in 200 times with 60 others online.
- A damaged FLAC file could make OpenTalk read outside a buffer when seeking, and a damaged WAV file could make it hang while opening the file (both found by fuzzing, in miniaudio's decoders). The vendored miniaudio carries a small patch for each, recorded in the third-party register. Fuzzing in CI now also reports any input that takes longer than 30 seconds.
- The fuzz build now instruments the libraries under test, not only the fuzz harnesses.
- Restoring a backup twice in the same second could overwrite the file being restored.

- A kicked or banned client no longer reconnects by itself, and now shows the server's reason. An error without a request ID now ends the session (documented in the protocol).
- TCP connections now send small messages immediately (`TCP_NODELAY`); before, a message could wait about 40 ms.
- The main window no longer receives connection signals while it is being destroyed.

- Text chat: accounts (Argon2id) and guests, channels (temporary and persistent, passwords, user limits), presence, channel, private and server-wide messages, with permissions enforced by the server for three fixed roles.
- Desktop app: connect dialog with saved servers, trust-on-first-use prompt, channel tree with users, chat history, private messages, automatic reconnection, and hosting a server from the app.
- Screen reader announcements for messages and people joining or leaving, combined and spaced to avoid flooding, each switchable in Settings.
- `opentalk-server`: data folder with an SQLite database, automatic self-signed certificate, account commands, `--allow-guests`, `--stop-when-stdin-closes`.
- SDK 0.2: login, channels, users and text.
- Windows builds get OpenSSL and libsodium from vcpkg pinned to one commit.

- Protocol version 1 control channel: CBOR session messages (Hello, Welcome, Ping, Pong, Goodbye, Error), strict validation, client and server session state machines with heartbeats, timeouts and rate limiting.
- TLS 1.3 for all connections using Qt's OpenSSL backend on every platform (ADR 0007); certificate fingerprints and trust-on-first-use pinning.
- Headless `opentalk-server` with connection limits and graceful shutdown.
- SDK 0.1 (`opentalk::sdk::Client`) and the `opentalk-ping` example tool.
- Integration tests over real TLS, including hostile clients; process-level tests of the real programs; fuzzers for messages and both session sides.
- Windows portable zip now includes the server, the ping tool, OpenSSL and the Visual C++ runtime, and is smoke-tested in CI.

- Project foundation: requirements, architecture, protocol proposal, accessibility plan, threat model, moderation model, licensing policy and roadmap.
- CMake build with presets for Windows (MSVC) and Linux, including sanitizer and fuzzing presets.
- Desktop window with channel, user, history and message panes; F6 and Ctrl+1–4 keyboard navigation; status bar announcements on Qt 6.8 and later.
- Settings window with General, Accessibility, and Logging and Diagnostics pages.
- Validated, crash-safe settings storage with migration support; installed, portable and custom data folders.
- Logging with per-component categories and a runtime log level.
- Protocol framing and version negotiation, with unit tests and a fuzz harness.
- CI for Windows (build, tests, portable zip) and Linux (format check, tests, sanitizers, fuzzing).

[Unreleased]: https://github.com/donovanmalisch23-alt/opentalk/compare/v1.0.0-rc.1...HEAD
[1.0.0-rc.1]: https://github.com/donovanmalisch23-alt/opentalk/releases/tag/v1.0.0-rc.1
