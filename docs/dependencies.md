# Dependency evaluation

**Rule:** a dependency is added only together with the code that needs it, after an entry in the [third-party register](licensing/third-party-register.md). License facts below come from the projects' published licenses as understood today. Every "verify" item must be checked against the exact version before adoption.

## In use now

| Dependency | Use | License | Notes |
|---|---|---|---|
| Qt 6: Core, Gui, Widgets, Network, Test | Application framework, UI, networking and TLS, tests | LGPL-3.0 / GPL-2.0 / GPL-3.0 (open source) | Dynamically linked. Obligations in [licensing policy](licensing/policy.md#qt). |
| OpenSSL 3 | TLS 1.3 through Qt's OpenSSL backend ([ADR 0007](architecture/decisions/0007-openssl-tls-backend.md)) | Apache-2.0 | Shipped with Windows builds. Must be a supported release series before the first public release. |
| CMake, Ninja | Build | BSD-3-Clause, Apache-2.0 | Build tools only; not shipped. |

## Candidates, by phase

| Candidate | Purpose | License | Phase | Questions to verify |
|---|---|---|---|---|
| Qt Network `QDtls` | Possibly DTLS for audio | LGPL-3.0 / GPL | 4 | DTLS 1.3 support; behaviour on Windows with the OpenSSL backend. |
| OpenSSL 3.5 LTS | Supported OpenSSL for release packages | Apache-2.0 | 8 | Trusted, pinned binary source or a build from source (vcpkg). |
| Opus (libopus) | Voice codec | BSD-3-Clause, plus a royalty-free patent licence | 4 | Build from source with pinned version via vcpkg or FetchContent; licence notice text. |
| miniaudio | Audio I/O backend (WASAPI, including loopback; also CoreAudio and PulseAudio/ALSA later) | Public domain (Unlicense) or MIT-0 | 4 | Compare latency and hot-plug with Qt Multimedia and raw WASAPI in the Phase 4 spike. |
| Qt Multimedia | Alternative audio I/O; media decoding for the media player | LGPL-3.0 / GPL | 4/7 | Windows builds use an FFmpeg backend; check that the shipped FFmpeg build is LGPL and which codecs it contains. |
| libsodium | Argon2id password hashing; AEAD if needed for audio | ISC | 3 | Prebuilt Windows binaries versus building from source. |
| SQLite (through Qt SQL) | Server database | Public domain | 3 | Qt's bundled SQLite version; WAL mode; backups through the online backup API. |
| speexdsp | Resampling, simple preprocessing | BSD-3-Clause | 4 | Could be replaced by Opus's internal resampler or `ma_resampler` from miniaudio. |
| RNNoise | Optional noise suppression | BSD-3-Clause | 7 | CPU cost; model data licence. |
| WebRTC audio processing (standalone build) | Echo cancellation, AGC | BSD-3-Clause | 7 | Build complexity; maintained fork to use. |
| vcpkg (manifest mode, pinned baseline) | Reproducible C/C++ dependency builds on Windows | MIT | 3/4 | How it interacts with Qt from the Qt installer. Qt itself stays installed by the official installer or aqtinstall. |
| WiX Toolset v4+ / MSIX | Windows installer | MS-RL (WiX) | 8 | MS-RL applies only to the WiX tool, not the output; verify. |

## Explicitly not planned

- **TeamTalk 5 SDK**: proprietary; against the project's goals.
- **Any GPL-2.0-only library**: incompatible with GPL-3.0-only.
- **Custom cryptography**: never.

## Security updates

Versions are pinned: the Qt version in CI, and later a vcpkg baseline commit. A monthly maintenance task, plus GitHub security advisories, triggers updates. Each update is a normal pull request that runs the full CI, so builds stay reproducible and updates are visible.
