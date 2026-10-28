# Dependency evaluation

**Rule:** a dependency is added only together with the code that needs it, after an entry in the [third-party register](licensing/third-party-register.md). License facts below come from the projects' published licenses as understood today. Every "verify" item must be checked against the exact version before adoption.

## In use now

| Dependency | Use | License | Notes |
|---|---|---|---|
| Qt 6: Core, Gui, Widgets, Network, Test | Application framework, UI, networking and TLS, tests | LGPL-3.0 / GPL-2.0 / GPL-3.0 (open source) | Dynamically linked. Obligations in [licensing policy](licensing/policy.md#qt). |
| Qt SQL with SQLite | Server database | LGPL-3.0 / GPL; SQLite public domain | Qt's bundled SQLite driver. |
| OpenSSL 3 | TLS 1.3 through Qt's OpenSSL backend ([ADR 0007](architecture/decisions/0007-openssl-tls-backend.md)); certificate generation | Apache-2.0 | Windows: vcpkg (pinned commit). Must be a supported release series before the first public release. |
| libsodium | Argon2id password hashing | ISC | Windows: vcpkg (pinned commit); Linux: system package. |
| Opus (libopus) | Voice and media codec | BSD-3-Clause | Windows: vcpkg; Linux: system package ([ADR 0009](architecture/decisions/0009-audio-io-miniaudio.md)). |
| miniaudio | Audio devices, loopback capture, decoding WAV/FLAC/MP3 | Unlicense or MIT-0 | Vendored, unmodified ([ADR 0009](architecture/decisions/0009-audio-io-miniaudio.md)). |
| speexdsp | Noise suppression, automatic gain, echo cancellation | BSD-3-Clause | Windows: vcpkg; Linux: system package ([ADR 0010](architecture/decisions/0010-voice-processing-media-and-system-audio.md)). |
| vcpkg (manifest mode) | Builds OpenSSL and libsodium for Windows | MIT | Pinned with `builtin-baseline` in `vcpkg.json`; build tool only. |
| CMake, Ninja | Build | BSD-3-Clause, Apache-2.0 | Build tools only; not shipped. |

## Candidates, by phase

| Candidate | Purpose | License | Phase | Questions to verify |
|---|---|---|---|---|
| Qt Network `QDtls` | Possibly DTLS for audio | LGPL-3.0 / GPL | 4 | DTLS 1.3 support; behaviour on Windows with the OpenSSL backend. |
| OpenSSL LTS (3.5 or newer LTS) | Supported OpenSSL for release packages | Apache-2.0 | 8 | vcpkg's newest 3.5 entry (3.5.4) is old; a current LTS may need a custom port or overlay. |
| Qt Multimedia or FFmpeg | Decoding more media formats (AAC, Ogg Vorbis, Opus files) | LGPL-3.0 / GPL; FFmpeg depends on build options | Later | Not used in Phase 7 ([ADR 0010](architecture/decisions/0010-voice-processing-media-and-system-audio.md)): check that a shipped FFmpeg build is LGPL, and which codecs raise patent questions. |
| RNNoise | Better noise suppression for non-steady noise | BSD-3-Clause | Later | CPU cost; model data licence. Could sit behind `VoiceProcessor`. |
| WebRTC audio processing (standalone build) | Better echo cancellation and AGC | BSD-3-Clause | Later | Build complexity; maintained fork to use. Could replace speexdsp behind `VoiceProcessor`. |
| WiX Toolset v4+ / MSIX | Windows installer | MS-RL (WiX) | 8 | MS-RL applies only to the WiX tool, not the output; verify. |

## Explicitly not planned

- **TeamTalk 5 SDK**: proprietary; against the project's goals.
- **Any GPL-2.0-only library**: incompatible with GPL-3.0-only.
- **Custom cryptography**: never.

## Security updates

Versions are pinned: the Qt version in CI, and later a vcpkg baseline commit. A monthly maintenance task, plus GitHub security advisories, triggers updates. Each update is a normal pull request that runs the full CI, so builds stay reproducible and updates are visible.
