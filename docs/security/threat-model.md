# Threat model and security plan

**Status:** Designed, with the control-channel protections implemented: TLS 1.3, certificate pinning, bounded and fuzzed parsing of frames and messages, strict session state machines, rate and connection limits, and timeouts. Also implemented: logs without message contents, and crash-safe settings. This document is reviewed at the start of each phase and whenever a trust boundary changes.

## Process

1. Draw the data flow for the phase's feature: components, data stores, trust boundaries.
2. Apply STRIDE (spoofing, tampering, repudiation, information disclosure, denial of service, elevation of privilege) to every element that crosses a boundary.
3. Record each threat with its mitigation, owner, test, and remaining risk in the table below.
4. Security-relevant pull requests link the threat IDs they address. Every fixed vulnerability gets a regression test.

## Assets

Account credentials and session tokens; server admin credentials and TLS private keys; private and channel messages; voice audio; moderation records and the audit log; user identities, IP addresses and presence; integrity of the client and server binaries.

## Trust boundaries

| ID | Boundary | Untrusted side |
|---|---|---|
| TB1 | Network ↔ server | Any client, or any attacker on the network |
| TB2 | Network ↔ client | The server (it may be malicious) and the network |
| TB3 | Server process ↔ its data directory | Other local users of the host |
| TB4 | Application ↔ out-of-process plugin | The plugin |
| TB5 | Application ↔ in-process plugin | None. Full trust, so first-party only |
| TB6 | User ↔ update and download channel | Network, mirrors |
| TB7 | Developer ↔ build pipeline and dependencies | Upstream dependencies, CI actions |
| TB8 | Moderator or administrator ↔ server policy | Moderators and administrators themselves (abuse of power) |

## Threats and mitigations

| ID | Threat (STRIDE) | Boundary | Mitigation | Verification |
|---|---|---|---|---|
| T1 | Malformed or oversized frames crash the parser or exhaust memory (D, E) | TB1, TB2 | Header-first size check, permanent failure after an error, 64 KiB limit; strict CBOR schemas; state-machine checks | Unit tests, TLS integration tests with hostile clients, three fuzzers (**implemented**) |
| T2 | Eavesdropping or modification in transit (I, T) | TB1, TB2 | TLS 1.3 control channel (**implemented**; TLS 1.2 refused); voice over UDP encrypted and authenticated with XChaCha20-Poly1305 using fresh per-login keys sent over TLS, voice over the TCP fallback protected by TLS (**implemented**, [ADR 0008](../architecture/decisions/0008-voice-encryption.md)) | `tst_tls_session`, `tst_packetcipher`, `tst_voice_session` |
| T3 | Server impersonation (S) | TB2 | Certificate pinning (trust on first use) or CA validation; mismatch is refused with an explanation (**implemented**; the confirm-and-remember UI arrives in Phase 3) | `tst_tls_session` (unknown and wrong pins) |
| T4 | Password guessing, credential stuffing (S) | TB1 | Argon2id (libsodium, interactive cost) on worker threads; failed logins take the same time for unknown accounts; 10 failures per address in 10 minutes block that address for the window; the connection closes after each failure (**implemented**) | `tst_servercore`, `tst_chat_session` |
| T5 | Privilege escalation by forged requests (E) | TB1 | Every operation is authorized server-side by the permission resolver (roles, ranks, channel rules, sanctions), never by fields sent by the client; the client only hides actions. Nobody can grant a capability they lack, give a role at or above their own, or act on an equal or higher rank (a channel moderator made by account rules may act on equals in that channel only) (**implemented**, Phase 5) | `tst_authorization` (182 request × role cases), `tst_moderation`, `tst_moderation_server`; `fuzz_server_core` drives arbitrary messages |
| T6 | Flooding: connections, messages, channel creation, audio (D) | TB1 | Per-address connection cap, total session cap, message token bucket, TLS handshake, Hello and idle timeouts, per-user chat flood limit, at most 1,000 channels and 3 temporary channels per user, Argon2id work limited to 2 threads; voice limited to 200 packets per second per session across UDP and TCP (excess dropped), tunnelled voice not queued for clients that fall behind (**implemented**) | Unit and integration tests (`tst_voicerouter`, `tst_session`); load tests in Phase 8 |
| T7 | A malicious server sends hostile text or names (T, E) | TB2 | Text is shown as plain text only, never rendered as HTML; length limits and rejection of control and bidirectional-override characters (**implemented** in the codec) | `tst_messages` |
| T8 | Disclosure through logs or support bundles (I) | All | No passwords, tokens or message contents logged; IP addresses only at the Detailed level; the user reviews a bundle before sharing | Log review; a test that searches logs for test secrets |
| T9 | Local secret theft (I) | TB3 | Client passwords are **not stored at all** for now (asked on each connect; Credential Manager support planned). Server password hashes are Argon2id. The server key file is owner-only on Unix; on Windows it relies on the per-user data folder's permissions | `tst_certificate`; manual check on Windows |
| T10 | A plugin abuses its access (I, T, E) | TB4 | One process per plugin; the app sends only granted events and refuses ungranted requests; consent per capability, renewed whenever the manifest changes; message, line-length and rate limits; misbehaving or crash-looping plugins turned off; the program must be inside the plugin's folder; `--no-plugins` safe start (**implemented**). **Not mitigated:** a plugin is a program running with the user's rights and is not sandboxed by the OS; the consent dialog says so | `tst_plugin_manifest`, `tst_plugins` (enforcement, floods, oversized lines, garbage, hangs, crashes), `tst_plugin_ui` |
| T11 | Replay, forgery or reflection of voice packets; impersonating another speaker (S, T) | TB1 | Per-direction keys; counter nonce with the header as associated data; 64-packet replay window that only authentic packets move; the server stamps the speaker from the keys, never from the packet (**implemented**) | `tst_packetcipher`, `tst_voicerouter`, `fuzz_voice_router` |
| T12 | Protocol downgrade (T) | TB1 | Version chosen from the intersection; the client rejects versions outside its range; TLS 1.3 minimum | `tst_version`, `tst_session`, `tst_tls_session` (**implemented**) |
| T13 | Tampered release binaries (T) | TB6 | Signed releases (needs a code-signing certificate, which is an owner decision); published SHA-256 sums; HTTPS-only update checks | Release checklist |
| T14 | Compromised dependency or CI action (T) | TB7 | Pinned versions, dependency review, `permissions: contents: read` in CI, a plan to pin actions by commit SHA | CI configuration review |
| T15 | Moderator abuse; erasing the audit trail (R, E) | TB8 | Hash-chained audit log checked at start-up and on request, with an anchor so retention pruning keeps the rest checkable; no way to delete it through OpenTalk; export for off-machine copies; rank rules; non-delegable owner capabilities; required reasons; separation of duties for appeals (**implemented**). Someone with file access can still rewrite the database; the chain shows that it happened. See the [moderation model](moderation-model.md) | `tst_moderation` (tampering detected), `tst_serverdatabase` (pruning), `tst_moderation_server`, `tst_server_process` (`--verify-audit`, `--export-audit`) |
| T16 | Owner lock-out (D) | TB8 | `--recover-owner` works only offline with access to the data directory, and is written to the audit log; administrators cannot remove or demote the owner (**implemented**) | `tst_server_process` (recovery), `tst_authorization` |

## Secure development practices

- Warnings as errors on MSVC, GCC and Clang. AddressSanitizer and UBSan run the full test suite in CI (**in place**).
- A fuzz harness for every parser of untrusted input, with a smoke run in CI (**in place** for framing, messages, sessions, the server core and voice datagrams).
- RAII and clear ownership; no raw `new` without a Qt parent or a smart pointer; bounded buffers; documented thread ownership.
- No custom cryptographic algorithms. Only libsodium, OpenSSL or the platform TLS stack, configured from reviewed defaults.
- Pull requests that touch protocol, authentication, permissions or plugins need a second reviewer once the project has more than one maintainer.
- Planned before Phase 3 ends: static analysis (clang-tidy and MSVC `/analyze`) in CI, and automated dependency vulnerability alerts.

## Privacy and retention (Phases 3 and 4)

- **Text messages** are relayed and **not stored** by the server. Clients keep history only in the open window.
- **Voice** is forwarded and **not decoded, recorded or stored** by the server. Voice keys exist only in memory for the length of a login.
- **Server database:** usernames, roles, Argon2id password hashes and creation times; persistent channels and their password hashes. Nothing about connections or messages.
- **Server logs** at the default level: session numbers, client software names, usernames at login, and channel IDs. No IP addresses (Detailed level only), no message text, and no passwords.
- **Client files:** settings, the saved-server list (name, address, port, username or nickname, trusted fingerprint; no passwords), and the log file.

## Not promised

- **End-to-end encryption:** the server can read the text and audio it relays. This is stated in the user documentation.
- **Protection against a malicious server owner** beyond transparency: owners control their own machines.
- **Anonymity:** the server sees connecting IP addresses.
