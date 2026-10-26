# Protocol proposal (version 1, provisional)

**Status:** Framing and version negotiation are implemented, unit-tested and fuzzed (`src/protocol`). Everything else here is designed. See [ADR 0005](../architecture/decisions/0005-transport-and-serialization.md).

## Goals

- Reliable, ordered control messages; low-latency, loss-tolerant audio.
- Every byte from the network is untrusted.
- Usable by the GUI, the headless admin tools, bots and future clients. It is not shaped around one screen.
- Versioned, with explicit compatibility rules.

## Transports

| Channel | Transport | Security | Default port (provisional) |
|---|---|---|---|
| Control | TCP | TLS 1.3 (TLS 1.2 only if a supported platform requires it, documented) | 10350/TCP |
| Audio | UDP | DTLS, or a reviewed AEAD scheme keyed from the TLS session (decision before Phase 4) | 10350/UDP |
| Audio fallback | Tunnelled inside the control channel | TLS | — |

Both ports are configurable. Port 10350 is a provisional choice, not an IANA assignment.

### Server identity

Most self-hosted servers will use self-signed certificates. The client therefore uses **trust on first use**. On first connection it shows the certificate fingerprint and asks the user to trust it. After that, the fingerprint is pinned per server profile, and a mismatch blocks the connection with a clear warning. Certificates from a public CA (for example Let's Encrypt) are validated normally and also work.

## Framing

All control traffic is a stream of frames (big-endian):

| Offset | Size | Field | Rule |
|---|---|---|---|
| 0 | 4 | Payload length | At most 64 KiB by default; larger frames close the connection. |
| 4 | 2 | Message type | See the registry below. |
| 6 | 2 | Flags | Must be 0 in version 1; non-zero closes the connection. |
| 8 | n | Payload | A CBOR map. |

The decoder rejects an oversized frame as soon as its header arrives, before buffering the payload, and it never recovers after an error. Both properties are tested and fuzzed.

## Session lifecycle

```
Client                                   Server
  │ TCP connect + TLS 1.3 handshake        │
  │── Hello {versions:[min,max], client} ─►│  server picks highest common version
  │◄─ Welcome {version, serverName,        │
  │           capabilities, limits}        │  or Error{UnsupportedVersion} and close
  │── Authenticate {username, password} ──►│  Argon2id verify; rate-limited
  │◄─ AuthResult {sessionId, userId,       │
  │              roles, audioKey/params}   │
  │◄─ StateSnapshot {channels, users}      │
  │── JoinChannel {channelId, password?} ─►│
  │◄─ events: UserJoined, UserLeft, ...    │
  │── Ping ─► / ◄─ Pong   every 15 s; 45 s of silence = timeout
  │── Goodbye ─► (or connection close)     │
```

- **Version negotiation:** the client sends a range and the server answers with one version. The client rejects any version outside its own range, which prevents silent downgrades. Implemented as `negotiateVersion`.
- **Requests and responses:** every client request carries a `requestId` (an unsigned integer chosen by the client), and the response echoes it. Server-initiated events have no `requestId`.
- **State machine:** the server accepts only the messages valid in the current session state (for example nothing but `Hello` before `Hello`). Anything else gets `Error{ProtocolViolation}` and the connection is closed.
- **Reconnection:** the client retries with exponential backoff (1, 2, 4 … up to 30 seconds, with jitter). A short-lived resume token from `AuthResult` lets it rejoin its channel without asking for the password again.

## Message type registry (initial)

| Range | Group |
|---|---|
| 0x0001–0x00FF | Session: Hello, Welcome, Authenticate, AuthResult, Ping, Pong, Goodbye, Error |
| 0x0100–0x01FF | Channels: StateSnapshot, Join, Leave, Create, Update, Delete, and their events |
| 0x0200–0x02FF | Users and presence |
| 0x0300–0x03FF | Text messages |
| 0x0400–0x04FF | Audio session control (codec parameters, transmit state); not the audio itself |
| 0x0500–0x05FF | Moderation and administration |
| 0x0600–0x06FF | Server management and monitoring |
| 0x7000–0x7FFF | Reserved for experiments; never used in releases |

Unknown message types get `Error{UnknownMessage}` but the connection is **not** closed, so newer clients stay compatible as long as framing is intact.

## Errors

Each `Error` has a numeric `code`, the `requestId` it answers (if any), and a short English `detail` meant for logs. The client builds its user-facing, translatable text from the code. Initial codes: `ProtocolViolation`, `UnsupportedVersion`, `UnknownMessage`, `MalformedMessage`, `AuthenticationFailed`, `NotAuthorized`, `NotFound`, `ChannelFull`, `WrongChannelPassword`, `RateLimited`, `ServerFull`, `Banned` (with expiry if temporary), `InternalError`.

## Audio packets (designed)

A UDP datagram, before encryption:

| Field | Size | Notes |
|---|---|---|
| Session-scoped sender ID | 4 | Assigned by the server; not the account ID. |
| Stream type | 1 | Voice, media or system audio, so receivers can label the source. |
| Sequence number | 2 | Wraps around; for loss detection and the jitter buffer. |
| Timestamp | 4 | In 48 kHz samples. |
| Opus payload | n | 20 ms frames by default; mono 48 kHz for voice. |

The server **forwards** packets to everyone else in the channel and does not mix them (a selective forwarding design). This keeps server CPU low and latency minimal. Clients mix. Audio is encrypted hop by hop, so the server can see it. That is what makes server-side moderation and recording possible, and it is stated plainly in the security documentation. It is **not** end-to-end encryption.

## Limits and rate limiting

Per session, the server enforces limits on frame size, messages per second (token bucket), channel creations per minute, failed logins per address (with exponential lockout) and text message length. The `Welcome` message advertises the limits so clients can avoid hitting them.

## Compatibility policy

- The protocol **major** version changes only for incompatible changes. A server supports at least the current and previous major version for one release cycle.
- New optional CBOR map keys and new message types are not breaking changes. Receivers ignore unknown keys.
- Deprecations are announced in release notes one release before removal.
