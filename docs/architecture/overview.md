# Architecture overview

**Principle:** one integrated desktop application, built from independent libraries that can be reused, tested and deployed separately. The server never needs a GUI.

## Components

Status uses the legend in [docs/README.md](../README.md).

| Component | Directory | Responsibility | Depends on | Status |
|---|---|---|---|---|
| Logging | `src/logging` | Log categories per component, file and stderr output, runtime level, single-line format. | QtCore | Implemented, unit-tested |
| Storage | `src/storage` | Data-folder layout (installed, portable or custom), typed settings schema, validated atomic settings store, migrations. Later: server database (SQLite) and backups. | QtCore, Logging | Implemented, unit-tested |
| Protocol | `src/protocol` | Wire format and rules: framing, version negotiation, CBOR session messages, client and server session state machines (handshake, heartbeats, timeouts, rate limits). No I/O. Later: channel, text and audio messages. | QtCore only | Implemented, unit-tested, fuzzed |
| UI | `src/ui` | Qt Widgets: main window, settings dialog, accessibility helpers. Presentation only; no network or audio logic. | QtWidgets, Storage, Logging | Implemented, automated a11y checks |
| App | `src/app` | Entry point. Composes the components, parses the command line, owns their lifetimes. | All of the above | Implemented |
| Client | `src/client` | `Connection`: TLS socket plus `ClientSession`, certificate trust, connect timeout. Later: reconnection, channel and user models, text. | Protocol, Security | Connection implemented and integration-tested; the rest is Phase 3 |
| Server | `src/server` | TLS listener, per-client sessions, connection limits, graceful shutdown, and the headless `opentalk-server` program. Later: accounts, channels, authorization, persistence. | Protocol, Security | Listener and sessions implemented and integration-tested; the rest is Phase 3+ |
| SDK | `src/sdk` | Stable public C++ API over Client; semantic versioning; [guide](../sdk/README.md); `examples/opentalk-ping`. | Client | Draft 0.1 implemented, tested through the example |
| Security | `src/security` | TLS 1.3 setup with the OpenSSL backend ([ADR 0007](decisions/0007-openssl-tls-backend.md)), certificate fingerprints, pinning decisions. Later: Argon2id hashing (libsodium), secure local secret storage. | QtNetwork | TLS and pinning implemented and tested |
| Audio | `src/audio` | `AudioBackend` interface (capture, playback, device events), Opus codec, jitter buffer, mixer, processing chain. Real-time thread rules below. | Opus, backend library | Designed (Phase 4) |
| Moderation | `src/moderation` | Roles, capabilities, permission resolution, sanctions, audit log, appeals. Pure logic with no networking, so it can be exhaustively unit tested. | QtCore, Storage | Designed (Phase 5) |
| Plugins | `src/plugins` | Manifest parsing, discovery, lifecycle, plugin host process and IPC, capability grants. | Protocol, Storage | Designed (Phase 6) |

### Dependency rules

```
app ─► ui ─► client ─► protocol
 │      │      ├─────► audio (Phase 4)
 │      │      └─────► security
 │      └────► storage ─► logging
 └────► server ─► moderation ─► storage
          ├─────► protocol
          └─────► security
sdk ─► client
plugins ─► protocol, storage
```

- Arrows point from user to dependency. There are no cycles, and CMake target links enforce the graph.
- `protocol`, `storage`, `moderation` and `server` must not link QtWidgets or QtGui. This keeps the server headless and portable.
- Platform-specific code lives in clearly named files behind interfaces (for example `audio/backends/WasapiBackend.cpp`), never scattered through `#ifdef`s.

## Processes and deployment

**Decision ([ADR 0004](decisions/0004-process-model.md)):** the client runs in the GUI process. The server is a library (`opentalk_server`) with two hosts:

1. `opentalk-server`: a headless console executable, also used later as a Windows service.
2. The desktop app, which **launches `opentalk-server` as a child process** when the user starts a local server, and manages it over the normal protocol (an admin connection to localhost).

Why a separate process and not a thread inside the GUI:

- A crash in the GUI or a plugin does not take the server and everyone connected to it down.
- The server keeps running when the window closes (optional), and the exact same binary runs on a VPS.
- Server management uses the same protocol as remote administration, so it is tested by everyday use.

The cost is a second executable and some IPC. That is small, and the protocol is needed anyway.

## Threading

| Thread | Owns | Must never |
|---|---|---|
| GUI thread | Widgets, view models, `SettingsStore` (client) | Block on network, disk larger than a settings file, or audio. |
| Network thread (client) | `QSslSocket` for control, UDP socket for audio | Touch widgets. It posts events to the GUI thread through queued signals. |
| Audio callback (real-time) | Capture and playback buffers | Allocate, lock a mutex, log, do I/O or call Qt. It talks to other threads through lock-free single-producer/single-consumer ring buffers only. |
| Audio worker | Opus encode and decode, jitter buffer, mixing, effects | Block on the network. |
| Server event loop(s) | Sessions and server state | Do slow disk I/O inline; that goes to a storage worker. |

## Data layout

See [data-layout.md](data-layout.md).

## Repository structure

```
OpenTalk/
├── CMakeLists.txt, CMakePresets.json   Build entry points and presets (Windows MSVC, Linux, ASan, fuzz)
├── cmake/                               Shared CMake modules (compiler warnings, sanitizers)
├── src/<component>/                     One static library per component
│   ├── include/opentalk/<component>/    Public headers of that component
│   └── *.cpp                            Implementation (private)
├── examples/                            Programs that use the public SDK (opentalk-ping)
├── tests/unit/                          Qt Test unit tests, one executable per test file
├── tests/integration/                   Real components over real sockets, and the real executables
├── tests/data/                          Test-only certificate and key
├── tests/fuzz/                          libFuzzer harnesses and seed inputs for code that reads untrusted input
├── docs/                                Design, decisions, plans, references
└── .github/workflows/                   CI
```

Directories such as `plugins/examples` and `packaging/windows` are created by the phase that first needs them. Empty placeholder directories are deliberately avoided.
