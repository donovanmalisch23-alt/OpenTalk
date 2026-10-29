# Project brief

## 1. Executive summary

**OpenTalk** is an open-source, self-hosted platform for real-time voice and text conversation. People run their own servers, organise them into channels, and talk with low-latency voice and text. It is inspired by what TeamTalk 5 does well: persistent servers, a channel tree, push-to-talk, and a keyboard-driven desktop client that blind users can operate. It is built from scratch with its own protocol, SDK and code, licensed GPL-3.0-only.

**Intended users**

- Communities that want their own voice server without a central company in the middle. Examples include blind and visually impaired groups, gaming and hobby groups, schools, and support groups.
- Screen reader users, especially on NVDA, who need a voice client that is fully usable without sight. This is a primary audience, not an afterthought.
- Server administrators who need moderation tools that are accountable and hard to abuse.
- Developers who want to build bots, media players and integrations on a documented SDK.

**What sets it apart**

1. Accessibility is a release-blocking requirement and is checked with automated tests and manual NVDA testing.
2. It is fully open: client, server, SDK and first-party plugins are all GPL-3.0-only, and there is no proprietary SDK underneath.
3. It is self-hosted, with no mandatory central service. One desktop application can both connect to servers and run one, and the server can also run without any window.
4. Moderation is accountable: the server enforces permissions, records who did what and why in an audit trail, and supports temporary sanctions and appeals.
5. It is extensible: plugins declare the capabilities they need, and risky plugins are designed to run in a separate process.
6. Portability: a documented data layout, a portable mode, and settings import and export.

**Main engineering challenges**, from highest risk down (details in [risk-assessment.md](risk-assessment.md)):

1. Low-latency audio on Windows: device handling, jitter buffering and a real-time thread that never blocks.
2. Encrypting the real-time audio path without inventing cryptography.
3. Screen reader behaviour of Qt widgets in practice (trees, live announcements) as opposed to on paper.
4. Plugin isolation. A C++ plugin loaded in-process has full privileges.
5. Sharing system audio without feedback loops, and the high cost of a real virtual audio driver.
6. Public-internet hosting for ordinary users: NAT, CGNAT, certificates and firewalls.
7. Keeping scope under control. The spec describes years of work, and the roadmap orders it so each stage ships something usable.

## 2. Requirements analysis

IDs are stable so issues, tests and documents can refer to them. "Phase" is the roadmap phase where the requirement is first delivered.

### 2.1 Functional requirements

| ID | Requirement | Phase |
|---|---|---|
| F-CONN-1 | Connect to a server by address, port and credentials; save server profiles. | 3 |
| F-CONN-2 | Clear, accessible error reporting for network, authentication and authorization failures. | 3 |
| F-CONN-3 | Automatic reconnection after transient network loss, with visible and announced state. | 3 |
| F-CHAN-1 | Browse a hierarchical channel tree; join and leave channels. | 3 |
| F-CHAN-2 | Create, edit and delete channels when authorized: name, description, password, user limit, persistent or temporary. | 3/5 |
| F-CHAN-3 | Defined behaviour for deleting non-empty channels, cleaning up empty temporary channels, and concurrent edits. | 3 |
| F-PRES-1 | User presence: stable user ID, display name, current channel, role indicators, mute and transmit state, optional status message. | 3 |
| F-TEXT-1 | Channel messages, server-wide broadcasts (authorized users only) and direct messages, with timestamps and delivery or failure state. | 3 |
| F-TEXT-2 | Message history with a documented, configurable retention policy and no undisclosed server-side retention. | 3/5 |
| F-VOICE-1 | Microphone capture, encoding, transmission, decoding and playback in channels. | 4 |
| F-VOICE-2 | Push-to-talk (global hotkey), optional voice activation, mute, deafen, input and output volume, per-user volume. | 4 |
| F-VOICE-3 | Device selection and recovery when a device is unplugged or changed. | 4 |
| F-VOICE-4 | Jitter buffering, packet-loss concealment and adaptation to congestion. | 4 |
| F-NOTIF-1 | Configurable sounds and text notifications per event category; replaceable soundpacks. | 3/4 |
| F-SRV-1 | Start, stop and restart a local server from the GUI; run the same server headless from the command line. | 3 |
| F-SRV-2 | Server configuration: interfaces, ports, limits, TLS certificate, accounts. | 3/5/8 |
| F-SRV-3 | Monitoring: connected users, channels, bandwidth, uptime, errors. | 5 |
| F-SRV-4 | Backup and restore of server data. | 8 |
| F-MOD-1 | Role and capability system with server and channel scopes and documented resolution rules. | 5 |
| F-MOD-2 | Sanctions: warning, mute, kick, temporary and permanent ban, restrictions; reasons; expiry. | 5 |
| F-MOD-3 | Tamper-evident audit trail, abuse reports, appeals and correcting mistaken sanctions. | 5 |
| F-PLUG-1 | Plugin manifest, discovery, enable, disable, remove, version compatibility checks. | 6 |
| F-PLUG-2 | Capability-based permissions for plugins; out-of-process hosting for risky capabilities. | 6 |
| F-PLUG-3 | Bots that authenticate as server accounts and are subject to the same authorization and audit. | 6 |
| F-MEDIA-1 | Media playback into a channel, labelled as media rather than a microphone. | 7 |
| F-MEDIA-2 | Optional audio effects pipeline (EQ, filters, voice changer) outside the real-time path when not real-time safe. | 7 |
| F-SYS-1 | System-audio sharing via WASAPI loopback (whole device, and per process where Windows supports it), with feedback prevention. | 7 |
| F-SDK-1 | Public SDK: connect, authenticate, channels, users, text, audio sessions, admin operations, errors, versions. | 2+ |
| F-DIAG-1 | Structured logs, support bundles that the user reviews before sharing, crash information. | 1/8 |
| F-CFG-1 | Typed, validated, migrated settings; import and export. | 1/8 |

### 2.2 Non-functional requirements

| ID | Requirement | How it is measured |
|---|---|---|
| N-LAT-1 | Mouth-to-ear latency on a LAN at or below 150 ms (provisional target; confirmed or revised by Phase 4 benchmarks). | Loopback measurement harness, documented setup. |
| N-AUD-1 | No audible dropouts at 5% random packet loss with the default jitter buffer. | Network emulation test. |
| N-CPU-1 | Client CPU below 5% of one core while receiving 4 speakers on a reference machine (provisional). | Performance test. |
| N-SCALE-1 | One server handles at least 100 connected users and 20 simultaneous speakers on modest hardware (provisional). | Load test with simulated clients. |
| N-UI-1 | The GUI thread never blocks on network, disk or audio; no UI freeze longer than 100 ms. | Code review rule plus instrumentation. |
| N-REL-1 | Settings and server data survive crashes: atomic writes, versioned migrations, backups before upgrades. | Unit tests (in place for client settings). |
| N-MAINT-1 | Warnings-as-errors builds, formatting enforced, components with one-way dependencies. | CI. |
| N-DOC-1 | Every public interface and setting is documented; documents mark implementation state. | Review checklist. |

### 2.3 Accessibility requirements

See [accessibility/plan.md](accessibility/plan.md) for the measurable acceptance criteria (A-1 to A-12). In summary: everything works with the keyboard alone; every control has an accessible name, role and state; live events are announced through the screen reader at a controllable verbosity; sound is never the only signal; focus is never lost or trapped; and every release candidate is tested manually with NVDA.

### 2.4 Security and privacy requirements

| ID | Requirement |
|---|---|
| S-1 | All client-server traffic encrypted and authenticated: TLS 1.3 for control; a standard, reviewed scheme for audio (decision pending, see protocol proposal). |
| S-2 | The server authorizes every operation; the GUI hiding a button is never the control. |
| S-3 | Passwords stored only as Argon2id hashes; no plaintext or hard-coded credentials. |
| S-4 | All network input treated as untrusted: size limits, type checks, state-machine checks, rate limits; parsers fuzzed. |
| S-5 | No passwords, tokens or private message contents in logs by default. |
| S-6 | Least privilege for users, bots and plugins; plugins declare capabilities. |
| S-7 | Published vulnerability disclosure policy and response process ([SECURITY.md](../SECURITY.md)). |
| S-8 | Documented retention for every kind of stored data. |

### 2.5 Licensing requirements

| ID | Requirement |
|---|---|
| L-1 | All first-party code GPL-3.0-only, with SPDX headers in every source file. |
| L-2 | Every dependency and asset recorded in the [third-party register](licensing/third-party-register.md) before it is merged. |
| L-3 | No TeamTalk code, assets or soundpacks unless redistribution rights are verified. TeamTalk is a functional reference only. |
| L-4 | Binary releases ship license texts and third-party notices, and meet the LGPL obligations for Qt. |

### 2.6 Deployment requirements

| ID | Requirement |
|---|---|
| D-1 | Windows x64 desktop: an installer and a portable zip. **Minimum: Windows 10** (version 1809 or later, which is Qt 6.8's minimum) and Windows 11. This is an owner decision from 2026-10-26. |
| D-2 | Routine client use never needs administrator rights. |
| D-3 | Headless server mode, usable on a LAN, a dedicated machine or a VPS. |
| D-4 | Documented ports, firewall and port-forwarding steps, and connectivity diagnostics. No mandatory relay or central service. |

### 2.7 Future-platform requirements

| ID | Requirement |
|---|---|
| P-1 | Platform-specific code (audio backend, secure storage, global hotkeys, installers) sits behind interfaces. |
| P-2 | Core libraries (protocol, storage, moderation, server) depend only on QtCore and portable libraries. |
| P-3 | Linux builds run in CI from day one (already true) so portability regressions show up early. Linux is not a supported *user* platform until its audio and accessibility have been validated. |
| P-4 | macOS and Linux support are claimed only after real builds, audio integration and screen reader validation (Orca, VoiceOver). |
