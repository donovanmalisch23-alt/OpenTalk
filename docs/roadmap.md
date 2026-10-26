# Roadmap

Each phase ends with an exit review against its definition of done. Milestone IDs (M1.1 and so on) are meant to become GitHub issues. Dependencies are listed explicitly; a phase can start before the previous one ends only where the dependencies allow.

## Phase 0: Research and foundation (done, pending owner approval)

Deliverables: [project brief](project-brief.md), [risk assessment](risk-assessment.md), [architecture](architecture/overview.md) and ADRs, [dependencies](dependencies.md), [licensing](licensing/policy.md), [protocol proposal](protocol/proposal.md), [accessibility plan](accessibility/plan.md), [threat model](security/threat-model.md), [moderation model](security/moderation-model.md), this roadmap.

**Exit:** the owner approves the architecture and answers the open decisions in [status.md](status.md#decisions-needed-from-the-project-owner).

## Phase 1: Buildable, accessible application skeleton (mostly done)

| ID | Milestone | Depends on | Status |
|---|---|---|---|
| M1.1 | CMake project, presets, warnings as errors, formatting | — | Done |
| M1.2 | Logging, data layout (installed, portable, custom), settings store | M1.1 | Done, unit-tested |
| M1.3 | Main window: panes, menus, status bar, F6 and Ctrl+number navigation | M1.2 | Done, automated a11y checks |
| M1.4 | Settings dialog: categories, apply and cancel, validation | M1.2 | Done for General, Accessibility, and Logging and Diagnostics |
| M1.5 | CI: Windows MSVC build, tests, portable zip; Linux tests with sanitizers; fuzz smoke run | M1.1 | Written; **first Windows run pending** |
| M1.6 | Manual NVDA pass with the [test script](accessibility/nvda-test-script.md) | M1.5 | **Open: needs the owner or a tester on Windows** |
| M1.7 | Visual identity: application icon, name styling, About box | — | Open |

**Definition of done:** a clean checkout builds and passes tests on Windows CI; the portable zip starts; the NVDA script passes or its failures are filed as bugs.

## Phase 2: Protocol and SDK foundation

| ID | Milestone | Depends on |
|---|---|---|
| M2.1 | CBOR message codec for session messages (Hello, Welcome, Error, Ping, Pong, Goodbye) with schema validation, fuzzed | Framing (done) |
| M2.2 | Session state machine, shared by client and server, unit-tested over an in-memory transport | M2.1 |
| M2.3 | TLS 1.3 transport (`QSslSocket`), test certificates, pinning logic | M2.2 |
| M2.4 | Minimal headless `opentalk-server` and a client library that complete the handshake over localhost | M2.3 |
| M2.5 | SDK public header draft with versioning policy | M2.4 |
| M2.6 | Decide the Windows TLS backend (Schannel or OpenSSL) with evidence; ADR | M2.3 |

**Exit:** an integration test starts the server, connects a client over TLS, negotiates the version, pings, and disconnects cleanly. Malformed and oversized input tests pass.

## Phase 3: Minimal end-to-end communication

Accounts with Argon2id (libsodium); authentication; the channel tree; join and leave; presence; channel and direct text messages; basic permissions (hard-coded roles); server profiles in the client; start and stop a local server from the GUI; reconnection; accessible errors and announcements (A-7, A-8); event sounds (an original soundpack).

**Exit:** two or more clients on a LAN can log in, browse, join channels and chat. The integration and NVDA scripts pass.

## Phase 4: Audio

Starts with a **spike**: compare miniaudio, Qt Multimedia and raw WASAPI for latency, dropouts and device hot-plug, and decide audio encryption (DTLS or AEAD) with an ADR. Then: capture, Opus, UDP transport, jitter buffer, playback and mixing, push-to-talk (global hotkey), voice activation, mute and deafen, volumes, device selection and recovery, TCP fallback, and a benchmark harness.

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

Public-hosting guide, reachability self-test, certificate setup, installer (and portable build), backups and restore, static analysis and dependency scanning in CI, longer fuzzing, resource-exhaustion tests, a release signing decision.

## Phase 9: Release candidate

Full regression; NVDA validation; upgrade testing; user, admin and plugin guides; third-party notices; release notes; the published acceptance checklist.

**Linux and macOS** are added only after Phase 4, once there is capacity for platform audio backends and Orca or VoiceOver testing. Linux CI already keeps the core code portable.
