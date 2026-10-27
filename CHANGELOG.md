# Changelog

All notable changes are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added

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
