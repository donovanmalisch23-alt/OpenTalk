# ADR 0005: TLS/TCP control channel with CBOR; separate UDP audio

**Status:** Provisional (needs a Phase 2 prototype and a Phase 4 audio-encryption decision)

**Context:** Control messages must be reliable and ordered. Voice must be low latency, and a lost packet is better than a late one. Everything must be encrypted with standard protocols.

**Decision:**

- **Control:** TCP with TLS 1.3 (`QSslSocket`). Length-prefixed frames ([framing implemented](../../protocol/proposal.md#framing)) carry CBOR-encoded messages. CBOR is binary, compact and self-describing, and Qt has it built in (`QCborStreamReader`/`QCborValue`), so it adds no dependency.
- **Audio:** UDP datagrams carrying Opus frames. The encryption choice is still open: DTLS (preferred) or a reviewed AEAD scheme keyed from the TLS session. If UDP is blocked, audio is tunnelled over the control channel and the UI shows a warning that latency will be higher.

**Rejected:**

- *Protocol Buffers:* a good option, but it adds a build-time code generator and a dependency. This will be reconsidered if hand-written CBOR schemas become error-prone.
- *JSON:* larger, slower, and no native binary type.
- *Everything over TCP:* head-of-line blocking hurts voice.
- *QUIC or WebRTC:* attractive, but large dependencies and complexity for the first version. Can be revisited.

**Validation:** Phase 2 parser fuzzing and round-trip tests. Phase 4 latency and loss measurements.
