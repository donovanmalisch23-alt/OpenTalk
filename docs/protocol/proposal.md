# Protocol specification (version 1, in progress)

**Status:** The control channel is **implemented, unit-tested, integration-tested over TLS and fuzzed** (`src/protocol`, `src/client`, `src/server`). That covers TLS 1.3, framing, the handshake, heartbeats, timeouts, rate limiting, errors, login, channels, presence and text. Audio is **designed** (Phase 4).

Protocol version 1 has not been released, so it was extended in Phase 3 rather than bumped. From the first public release on, the compatibility policy at the end of this document applies. See [ADR 0005](../architecture/decisions/0005-transport-and-serialization.md) and [ADR 0007](../architecture/decisions/0007-openssl-tls-backend.md).

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

Most self-hosted servers use self-signed certificates; `opentalk-server` creates one on first start. The client therefore uses **trust on first use**. On first connection it shows the certificate fingerprint and asks the user to trust it. After that, the fingerprint is pinned per server profile, and a mismatch blocks the connection with a clear warning. Certificates from a public CA (for example Let's Encrypt) are validated normally and also work.

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

- **Version negotiation:** the client sends a range and the server answers with one version. The client rejects any version outside its own range, which prevents silent downgrades. **Implemented.**
- **Login:** after Welcome the client sends `Authenticate` (an account and password, or a guest nickname). The server answers `AuthResult`, then `StateSnapshot`, and tells everyone else with `UserJoined`. Until login succeeds, only `Authenticate` (plus Ping, Pong and Goodbye) is accepted. A failed login gets `Error{AuthenticationFailed}` and the connection is closed. The message is the same whether the account exists or not.
- **Requests and responses:** every client request carries a `requestId` (an unsigned integer chosen by the client), and the response echoes it. Server-initiated events have no `requestId`.
- **State machine:** the server accepts only the messages valid in the current session state (for example nothing but `Hello` before `Hello`). Anything else gets `Error{ProtocolViolation}` and the connection is closed.
- **Reconnection:** the client retries with exponential backoff (1, 2, 4 … up to 30 seconds, with jitter). A short-lived resume token from `AuthResult` lets it rejoin its channel without asking for the password again.

## Message type registry (initial)

| Range | Group |
|---|---|
| 0x0001–0x00FF | Session: Hello, Welcome, Ping, Pong, Goodbye, Error, Authenticate, AuthResult |
| 0x0100–0x01FF | Channels: StateSnapshot, Join, Leave, Create, Update, Delete, and their events |
| 0x0200–0x02FF | Users and presence |
| 0x0300–0x03FF | Text messages |
| 0x0400–0x04FF | Voice: UDP setup and keys, and audio over this connection when UDP is blocked ([voice.md](voice.md)) |
| 0x0500–0x05FF | Moderation and administration |
| 0x0600–0x06FF | Server management and monitoring |
| 0x7000–0x7FFF | Reserved for experiments; never used in releases |

Unknown message types get `Error{UnknownMessage}` but the connection is **not** closed, so newer clients stay compatible as long as framing is intact. Before the handshake completes, an unknown type closes the connection.

## Session messages (implemented)

Payloads are CBOR maps with small integer keys. Clients accept server payloads up to 1 MiB (for state snapshots). Servers accept up to the limit they announce (64 KiB). Keys are scoped to each message type. Receivers ignore keys they do not know. Every field below is required unless marked optional.

| Type | Name | Direction | Fields (key: name, type) |
|---|---|---|---|
| 0x0001 | Hello | client → server | 1: min version (u16 ≥ 1), 2: max version (u16 ≥ min), 3: client name (text), 4: client version (text) |
| 0x0002 | Welcome | server → client | 1: chosen version (u16), 2: server name (text), 3: server version (text), 4: max payload size the server accepts (u32 ≥ 1024), 5: heartbeat interval in seconds (u16 ≥ 1) |
| 0x0003 | Ping | either | 1: token (unsigned, 63 bits) |
| 0x0004 | Pong | either | 1: token copied from the Ping |
| 0x0005 | Goodbye | either | 1: reason (error code, 0 = normal) |
| 0x0006 | Error | either | 1: code (u16), 2: request ID (optional, unsigned), 3: detail (text, may be empty) |

**Text rules:** names are 1–128 UTF-16 code units and not blank. Detail text is at most 1024. Control and invisible formatting characters (for example U+202E, which reverses text direction) are rejected, so a hostile peer cannot hide or disguise text shown to users or written to logs.

**Session rules:**

| Rule | Value (default) |
|---|---|
| Server waits for Hello after TLS | 10 s, then Error{Timeout} and close |
| Client waits for Welcome | 10 s |
| Heartbeat | The client pings every heartbeat interval announced in Welcome (15 s) |
| Server idle timeout | 45 s without any data: Goodbye{Timeout} and close |
| Client timeout | 3 heartbeat intervals without any data |
| Rate limit (server) | Token bucket: 50 messages per second sustained, bursts of 100; above that, Error{RateLimited} and close |
| Connection limits (server) | 200 sessions in total (configurable, extra clients get Goodbye{ServerFull}); 10 connections per IP address (extra ones dropped before TLS) |
| TLS handshake timeout (server) | 10 s |
| Shutdown | Server sends Goodbye{ServerShuttingDown} to everyone, waits up to 3 s, then closes |

**Error and Goodbye codes:** 0 None, 1 ProtocolViolation, 2 UnsupportedVersion, 3 UnknownMessage, 4 MalformedMessage, 5 RateLimited, 6 ServerFull, 7 Timeout, 8 ServerShuttingDown, 9 InternalError, 10 AuthenticationFailed, 11 NotAuthorized, 12 NotFound, 13 ChannelFull, 14 WrongChannelPassword, 15 InvalidRequest, 16 NameInUse. Codes are never renumbered. Unknown codes from newer peers are treated as InternalError.

Welcome also carries 6: guests allowed (bool, optional).

## Login, channels, presence and text (implemented, Phase 3)

| Type | Name | Direction | Fields |
|---|---|---|---|
| 0x0007 | Authenticate | C→S | 1: mode (0 account, 1 guest), 2: name, 3: password (account mode only; up to 1024 characters, any characters) |
| 0x0008 | AuthResult | S→C | 1: your user ID, 2: display name, 3: role (0 guest, 1 user, 2 admin) |
| 0x0100 | StateSnapshot | S→C | 1: array of channels, 2: array of users (at most 10,000 each) |
| 0x0101 | ChannelAdded | S→C | a channel (fields below) |
| 0x0102 | ChannelRemoved | S→C | 1: channel ID |
| 0x0110 | JoinChannel | C→S | 1: request ID, 2: channel ID, 3: password (optional) |
| 0x0111 | CreateChannel | C→S | 1: request ID, 2: parent ID, 3: name, 4: topic (optional), 5: persistent (bool, optional), 6: password (optional), 7: maximum users (optional, 0 = no limit) |
| 0x0112 | DeleteChannel | C→S | 1: request ID, 2: channel ID |
| 0x0120 | RequestDone | S→C | 1: request ID, 2: result ID (optional, for example a new channel's ID) |
| 0x0200 | UserJoined | S→C | a user (fields below) |
| 0x0201 | UserLeft | S→C | 1: user ID |
| 0x0202 | UserMoved | S→C | 1: user ID, 2: channel ID |
| 0x0300 | SendText | C→S | 1: request ID, 2: target (0 channel, 1 user, 2 whole server), 3: target ID, 4: text |
| 0x0301 | TextMessage | S→C | 1: target, 2: sender's user ID, 3: target ID, 4: text, 5: server time (ms since 1970, UTC) |
| 0x0400 | AudioSetup | S→C | 1: UDP port (0 = none), 2: UDP session ID, 3: client-to-server key (32 bytes), 4: server-to-client key (32 bytes), 5: maximum bit rate. Sent after login. |
| 0x0401 | AudioData | both | 1: stream (0 voice, 1 media, 2 computer audio), 2: sequence number, 3: Opus packet (1–1275 bytes), 4: speaker's user ID (server only) |

**Channel** fields: 1: ID, 2: parent ID (0 only for the root), 3: name (empty only for the root), 4: topic, 5: persistent, 6: has password, 7: maximum users. **User** fields: 1: ID, 2: name, 3: role, 4: channel ID.

**IDs.** User IDs are per connection and valid until the user disconnects; they are not account IDs. Channel 1 is the root. Persistent channels have IDs from 1000 and keep them across restarts. Temporary channels have IDs from 0x80000000.

**Requests** are answered by either `RequestDone` or `Error` with the same request ID. Every request is authorized by the server. The client hiding a menu item is never the security check.

**Rules the server enforces** (Phase 3 fixed roles; configurable roles come in Phase 5):

| Action | Guest | User | Admin |
|---|---|---|---|
| Join channels, send channel and direct messages | yes | yes | yes |
| Create temporary channels (at most 3 at a time) | no | yes | yes |
| Create persistent channels, delete channels | no | no | yes |
| Send server-wide messages | no | no | yes |
| Ignore channel passwords and user limits | no | no | yes |

- You can only write to the channel you are in. Direct messages go to one online user. The server echoes every message back to its sender with the server's time stamp.
- A temporary channel's creator moves into it. A temporary channel disappears when its last user leaves, and it cannot have sub-channels.
- Deleting a channel moves its users to the root first. A channel with sub-channels cannot be deleted.
- Channel names are unique among siblings, ignoring case.
- Guest nicknames may not match an account name or an online user's name (ignoring case).

**Text rules:** names (users, channels) are 1–32 or 1–64 characters, without control or invisible formatting characters, and without spaces at either end. Chat text is up to 2,000 characters. Newlines and tabs are allowed. Other control characters, and the bidirectional override and isolate characters (U+202A–U+202E, U+2066–U+2069) that can make text read differently from how it is stored, are rejected. Emoji sequences that use the zero-width joiner are allowed.

**Flood control:** besides the 50 messages per second session limit, each user may send about 1 text message per second, in bursts of up to 5. Extra messages get `Error{RateLimited}` and the connection stays open.

**Login protection:** passwords are checked with Argon2id on worker threads, so the server stays responsive. Unknown usernames are checked against a dummy hash, so a failed login takes the same time either way. After 10 failed logins from one IP address within 10 minutes, further attempts from that address are refused for the rest of the window.

**Retention:** the server relays text messages and **does not store them**. Message history exists only in each client's window for that session.

## Errors

Each `Error` has a numeric `code`, the `requestId` it answers (if any), and a short English `detail` meant for logs. The client builds its user-facing, translatable text from the code. All implemented codes are listed above. Planned: `Banned` (with expiry if temporary) in Phase 5.

## Audio (implemented in Phase 4)

Voice is specified in **[voice.md](voice.md)**. In short:

- Opus packets (20 ms, 48 kHz mono) travel in UDP datagrams encrypted with XChaCha20-Poly1305. The keys are sent in `AudioSetup` over this TLS connection.
- When UDP does not work, the same packets travel as `AudioData` messages over this connection.

The server **forwards** packets to everyone else in the channel and does not mix them (a selective forwarding design). This keeps server CPU low and latency minimal; clients mix. Audio is encrypted hop by hop, so the server can see it. That is what makes server-side moderation and recording possible, and it is stated plainly in the security documentation. It is **not** end-to-end encryption.

## Limits and rate limiting

Per session, the server enforces limits on frame size, messages per second (token bucket), channel creations per minute, failed logins per address (with exponential lockout) and text message length. The `Welcome` message advertises the limits so clients can avoid hitting them.

## Compatibility policy

- The protocol **major** version changes only for incompatible changes. A server supports at least the current and previous major version for one release cycle.
- New optional CBOR map keys and new message types are not breaking changes. Receivers ignore unknown keys.
- Deprecations are announced in release notes one release before removal.
