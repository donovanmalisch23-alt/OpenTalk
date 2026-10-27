# Roadmap

Each phase ends with an exit review against its definition of done. Milestone IDs (M1.1 and so on) are meant to become GitHub issues. Dependencies are listed explicitly; a phase can start before the previous one ends only where the dependencies allow.

## Phase 0: Research and foundation (done; approved by the owner on 2026-10-26)

Deliverables: [project brief](project-brief.md), [risk assessment](risk-assessment.md), [architecture](architecture/overview.md) and ADRs, [dependencies](dependencies.md), [licensing](licensing/policy.md), [protocol proposal](protocol/proposal.md), [accessibility plan](accessibility/plan.md), [threat model](security/threat-model.md), [moderation model](security/moderation-model.md), this roadmap.

**Exit:** the owner approves the architecture and answers the open decisions in [status.md](status.md#decisions-needed-from-the-project-owner).

## Phase 1: Buildable, accessible application skeleton (done except the NVDA pass and icon)

| ID | Milestone | Depends on | Status |
|---|---|---|---|
| M1.1 | CMake project, presets, warnings as errors, formatting | — | Done |
| M1.2 | Logging, data layout (installed, portable, custom), settings store | M1.1 | Done, unit-tested |
| M1.3 | Main window: panes, menus, status bar, F6 and Ctrl+number navigation | M1.2 | Done, automated a11y checks |
| M1.4 | Settings dialog: categories, apply and cancel, validation | M1.2 | Done for General, Accessibility, and Logging and Diagnostics |
| M1.5 | CI: Windows MSVC build, tests, portable zip; Linux tests with sanitizers; fuzz smoke run | M1.1 | Done: Windows build and tests pass |
| M1.6 | Manual NVDA pass with the [test script](accessibility/nvda-test-script.md) | M1.5 | **Open: needs the owner or a tester on Windows** |
| M1.7 | Visual identity: application icon, name styling, About box | — | Open |

**Definition of done:** a clean checkout builds and passes tests on Windows CI; the portable zip starts; the NVDA script passes or its failures are filed as bugs.

## Phase 2: Protocol and SDK foundation (done)

| ID | Milestone | Status |
|---|---|---|
| M2.1 | CBOR codec for session messages (Hello, Welcome, Error, Ping, Pong, Goodbye) with strict validation, fuzzed | Done |
| M2.2 | Client and server session state machines, tested over an in-memory pipe with a fake clock | Done (22 tests) |
| M2.3 | TLS 1.3 transport, test certificate, pinning logic | Done |
| M2.4 | Headless `opentalk-server` and client `Connection` completing the handshake over localhost | Done; also tested as real processes |
| M2.5 | SDK public header draft with versioning policy | Done: [SDK 0.1](sdk/README.md) and `opentalk-ping` |
| M2.6 | Windows TLS backend decision | Done: [ADR 0007](architecture/decisions/0007-openssl-tls-backend.md), OpenSSL everywhere |

**Exit criterion met:** an integration test starts the server, connects a client over TLS 1.3, negotiates the version, pings and disconnects cleanly. Malformed input, oversized frames, plaintext, TLS 1.2-only, silent, flooding and wrong-certificate clients are refused (`tst_tls_session`, `tst_session`). Three fuzzers cover the parsing and session code.

## Phase 3: Minimal end-to-end communication (done, except the NVDA pass)

| ID | Milestone | Status |
|---|---|---|
| M3.1 | Dependencies: libsodium, OpenSSL (vcpkg, pinned) and Qt SQL | Done; Windows packages now ship OpenSSL 3.6.5 instead of the end-of-life 3.0 |
| M3.2 | Server data folder, SQLite database with migrations, Argon2id accounts, account commands, automatic self-signed certificate | Done |
| M3.3 | Protocol messages for login, channels, presence and text; `ServerCore` with fixed roles; `ClientState` | Done; 21 server rule tests, 76 codec cases, fuzzed |
| M3.4 | Network wiring, request API, automatic reconnection, SDK 0.2 | Done; multi-client end-to-end tests including a server restart |
| M3.5 | Desktop app: saved servers, trust prompt, channel tree, chat, private messages, announcements | Done; tested against a real server |
| M3.6 | Host a server from the app; documentation | Done |
| M3.7 | Manual NVDA pass of the [script](accessibility/nvda-test-script.md) (steps 19–30 are new) | **Open: needs the owner or a tester** |

**Moved out of Phase 3:**

- **Event sounds** move to **Phase 4**, so they use the new audio output path instead of a temporary one. Every event already has a history line, and most have an announcement.
- **Remembering passwords** (Windows Credential Manager) moves to **Phase 8**. Today the app asks for the password on each connect and never stores it.

**Exit criterion met (automated):** two or more clients on a server log in, browse channels, join, create and delete channels, and exchange channel and private messages. This is tested both with the client library and with the real main window. Remaining for full sign-off: the NVDA pass.

## Phase 4: Audio

Starts with a **spike**: compare miniaudio, Qt Multimedia and raw WASAPI for latency, dropouts and device hot-plug, and decide audio encryption (DTLS or AEAD) with an ADR. Then: capture, Opus, UDP transport, jitter buffer, playback and mixing, push-to-talk (global hotkey), voice activation, mute and deafen, volumes, device selection and recovery, TCP fallback, a benchmark harness, and **event sounds** with an original soundpack.

**Exit:** several clients exchange clear voice, with measured latency, CPU and loss-tolerance numbers published alongside the test setup.

## Phase 5: Administration and moderation

The moderation library (roles, ranks, resolution, sanctions, audit chain, reports, appeals); server configuration UI; monitoring; owner recovery; the authorization test matrix.

**Exit:** every admin action is enforced by the server and tested against unauthorized roles; the audit chain is verified; the UI is accessible.

## Phase 6: Plugins

Manifest, plugin host process, IPC, capability grants and the consent UI, an example plugin, bot accounts, a plugin licensing policy (with legal review).

**Exit:** an independently built example plugin installs, runs, is disabled and is removed through the documented workflow.

## Phase 7: Advanced audio and system-audio sharing

Effects pipeline; media player plugin; per-process WASAPI loopback that excludes OpenTalk's own output; whole-device loopback with feedback warnings; a virtual-driver feasibility report, with cost, for an owner decision.

## Phase 8: Hosting, deployment and hardening

Public-hosting guide, reachability self-test, certificate setup, **packaged OpenSSL on a supported LTS series, with licence notices for all bundled libraries**, remembered passwords in Windows Credential Manager, installer (and portable build), backups and restore, static analysis and dependency scanning in CI, longer fuzzing, resource-exhaustion tests, a release signing decision.

## Phase 9: Release candidate

Full regression; NVDA validation; upgrade testing; user, admin and plugin guides; third-party notices; release notes; the published acceptance checklist.

**Linux and macOS** are added only after Phase 4, once there is capacity for platform audio backends and Orca or VoiceOver testing. Linux CI already keeps the core code portable.
