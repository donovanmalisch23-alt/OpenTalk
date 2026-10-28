# Architecture decision records

Short records of significant decisions: context, options, decision, consequences. A record marked **Provisional** may still change before the phase that depends on it. A superseded record is kept and linked to the record that replaces it.

| # | Title | Status |
|---|---|---|
| [0001](0001-cpp20-qt6-widgets-cmake.md) | C++20, Qt 6 Widgets and CMake | Accepted |
| [0002](0002-license-gpl-3-only.md) | GPL-3.0-only for all first-party code | Accepted (owner requirement) |
| [0003](0003-settings-json-schema.md) | Client settings as schema-validated JSON | Accepted |
| [0004](0004-process-model.md) | Server as a library with a headless host process | Accepted |
| [0005](0005-transport-and-serialization.md) | TLS/TCP control channel with CBOR; separate UDP audio | Accepted; audio encryption decided in 0008 |
| [0006](0006-plugin-isolation.md) | Two-tier plugin model, out-of-process by default | Provisional |
| [0007](0007-openssl-tls-backend.md) | Qt's OpenSSL TLS backend on every platform | Accepted |
| [0008](0008-voice-encryption.md) | Voice encryption: XChaCha20-Poly1305 with keys sent over TLS | Accepted |
| [0009](0009-audio-io-miniaudio.md) | miniaudio for audio devices; Opus for coding | Accepted |
