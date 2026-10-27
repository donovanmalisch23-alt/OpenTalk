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
| T2 | Eavesdropping or modification in transit (I, T) | TB1, TB2 | TLS 1.3 control channel (**implemented**; TLS 1.2 refused); encrypted and authenticated audio (Phase 4) | `tst_tls_session` |
| T3 | Server impersonation (S) | TB2 | Certificate pinning (trust on first use) or CA validation; mismatch is refused with an explanation (**implemented**; the confirm-and-remember UI arrives in Phase 3) | `tst_tls_session` (unknown and wrong pins) |
| T4 | Password guessing, credential stuffing (S) | TB1 | Argon2id; per-account and per-address backoff; optional lockout | Rate-limit tests |
| T5 | Privilege escalation by forged requests (E) | TB1 | Every operation authorized server-side against the session's account, never against fields sent by the client | A test matrix of "unauthorized role × every admin message" |
| T6 | Flooding: connections, messages, channel creation, audio (D) | TB1 | Per-address connection cap, total session cap, message token bucket, TLS handshake, Hello and idle timeouts (**implemented**); channel limits later | Unit and integration tests; load tests in Phase 8 |
| T7 | A malicious server sends hostile text or names (T, E) | TB2 | Text is shown as plain text only, never rendered as HTML; length limits and rejection of control and bidirectional-override characters (**implemented** in the codec) | `tst_messages` |
| T8 | Disclosure through logs or support bundles (I) | All | No passwords, tokens or message contents logged; IP addresses only at the Detailed level; the user reviews a bundle before sharing | Log review; a test that searches logs for test secrets |
| T9 | Local secret theft (I) | TB3 | Client passwords in Windows Credential Manager; server key and database with user-only file permissions | Manual check; documentation |
| T10 | A plugin abuses its access (I, T, E) | TB4 | Out-of-process plugins, capability grants enforced at the IPC layer, user consent for sensitive capabilities | Phase 6 tests |
| T11 | Replay of audio packets (T) | TB1 | Sequence numbers with a replay window inside the authenticated encryption | Phase 4 tests |
| T12 | Protocol downgrade (T) | TB1 | Version chosen from the intersection; the client rejects versions outside its range; TLS 1.3 minimum | `tst_version`, `tst_session`, `tst_tls_session` (**implemented**) |
| T13 | Tampered release binaries (T) | TB6 | Signed releases (needs a code-signing certificate, which is an owner decision); published SHA-256 sums; HTTPS-only update checks | Release checklist |
| T14 | Compromised dependency or CI action (T) | TB7 | Pinned versions, dependency review, `permissions: contents: read` in CI, a plan to pin actions by commit SHA | CI configuration review |
| T15 | Moderator abuse; erasing the audit trail (R, E) | TB8 | Hash-chained audit log, non-delegable owner capabilities, required reasons, separation of duties for appeals. See the [moderation model](moderation-model.md) | Phase 5 tests |
| T16 | Owner lock-out (D) | TB8 | Offline recovery command that needs access to the data directory | Phase 5 test |

## Secure development practices

- Warnings as errors on MSVC, GCC and Clang. AddressSanitizer and UBSan run the full test suite in CI (**in place**).
- A fuzz harness for every parser of untrusted input, with a smoke run in CI (**in place for framing**).
- RAII and clear ownership; no raw `new` without a Qt parent or a smart pointer; bounded buffers; documented thread ownership.
- No custom cryptographic algorithms. Only libsodium, OpenSSL or the platform TLS stack, configured from reviewed defaults.
- Pull requests that touch protocol, authentication, permissions or plugins need a second reviewer once the project has more than one maintainer.
- Planned before Phase 3 ends: static analysis (clang-tidy and MSVC `/analyze`) in CI, and automated dependency vulnerability alerts.

## Not promised

- **End-to-end encryption:** the server can read the text and audio it relays. This is stated in the user documentation.
- **Protection against a malicious server owner** beyond transparency: owners control their own machines.
- **Anonymity:** the server sees connecting IP addresses.
