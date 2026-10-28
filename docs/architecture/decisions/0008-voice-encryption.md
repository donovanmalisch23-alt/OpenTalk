# ADR 0008: Voice encryption with XChaCha20-Poly1305, keys sent over TLS

**Status:** Accepted (Phase 4)

**Context:** Voice travels as UDP datagrams ([ADR 0005](0005-transport-and-serialization.md)). Every datagram must be confidential and authenticated, replays must be rejected, and a lost or reordered packet must not stop the stream. ADR 0005 left two options open: DTLS, or an AEAD scheme keyed from the TLS session. The project rule is **no custom cryptography**: only standard, reviewed constructions from established libraries.

**Options:**

1. **DTLS 1.2 with Qt's `QDtls`** (OpenSSL backend). A second handshake per client, with its own certificate checks and cookie exchange; every packet carries a DTLS record header. Qt has no DTLS 1.3.
2. **AEAD with per-session keys delivered over the existing TLS 1.3 connection.** The server creates two random 256-bit keys per session (one per direction) and sends them inside the already authenticated, encrypted control connection. Each datagram is sealed with XChaCha20-Poly1305 (libsodium's `crypto_aead_xchacha20poly1305_ietf`), using a 64-bit packet counter as the nonce and the clear-text header as associated data.
3. SRTP with keys over TLS (SDES-style). Standard, but needs another library (libsrtp) and an RTP layer the project does not otherwise need.

**Decision:** Option 2.

**Why:**

- **The keys inherit TLS 1.3's guarantees:** server authentication (including the trust-on-first-use pin), forward secrecy and confidentiality. No second handshake can be attacked or misconfigured.
- **It uses a standard AEAD from a well-reviewed library already in the project** (libsodium, also used for Argon2id). The only construction we wrote is the packet framing: counter nonce, header as associated data, and a replay window. This is the same pattern as WireGuard, QUIC and SRTP; it is not new cryptography.
- **It is cheap:** 29 bytes of overhead per packet and no handshake latency. Joining a channel needs no new key exchange.
- **It is easy to test** without sockets (`PacketSealer`/`PacketOpener` are pure functions of their input), so tampering, forgery, reflection and replay are covered by unit tests and fuzzing.

**Design details** (full layout in [protocol/voice.md](../../protocol/voice.md)):

- **Keys:** 32 random bytes from the operating system's generator via `randombytes_buf`, one key per direction per session, new on every login, never stored, never reused.
- **Nonce:** counter (8 bytes, big-endian), direction byte, 15 zero bytes. Separate keys per direction already prevent a nonce from repeating; the direction byte adds a second guard against reflected packets. XChaCha20's 192-bit nonce leaves room for this structure.
- **Associated data:** the 13-byte clear-text header (kind, UDP session ID, counter). Changing any header bit makes the packet fail authentication.
- **Replay protection:** a 64-packet sliding window. Only authentic packets move the window, so forged packets with huge counters cannot push genuine ones out.
- **Identity:** the server stamps the speaker's user ID from the keys that authenticated the packet, never from the packet's own field.
- **Counter exhaustion:** at 50 packets per second, 2^64 packets will not be reached. A sealer refuses to wrap anyway.

**Consequences:**

- **Voice is not end-to-end encrypted:** the server can decrypt and re-encrypt voice, as with TeamTalk, Mumble and most self-hosted voice servers. This matches the threat model (the server operator is trusted with channel traffic). End-to-end encryption would need a group key agreement and is out of scope; it is recorded in the threat model.
- **When UDP is blocked,** the same audio packets travel inside the TLS connection (`AudioData` messages). These are protected by TLS instead of the AEAD.
- **A stolen key exposes only that session** (until logout or disconnect).

**Validation:**

- Unit tests (`tst_packetcipher`): round trip; every single-bit change rejected; wrong key, session or direction rejected; replay and window edges; forgeries do not move the window.
- Router tests (`tst_voicerouter`): identity stamping, path selection, flood limits.
- Fuzzing (`fuzz_voice_router`) of raw and correctly sealed hostile datagrams.
- End-to-end test over real UDP sockets (`tst_voice_session`).
