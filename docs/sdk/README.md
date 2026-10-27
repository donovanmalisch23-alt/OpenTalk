# OpenTalk SDK

**Status: draft (SDK 0.1).** It is implemented and covered by tests through the `opentalk-ping` example. It can connect, show server information, measure round-trip time and disconnect. Channels, users, text and audio are added as the server gains them.

The SDK is the supported way for bots, tools and other programs to talk to OpenTalk servers. The OpenTalk desktop application is built on the same internal libraries.

## Using it

The public headers are `include/opentalk/sdk/Client.h` and `Version.h`. Link the `OpenTalk::Sdk` CMake target. You need Qt 6 (Core and Network) and a running Qt event loop.

```cpp
#include <opentalk/sdk/Client.h>

QString error;
if (!opentalk::sdk::Client::initialize(&error)) { /* TLS 1.3 unavailable */ }

opentalk::sdk::Client client;
client.setApplication("my-bot", "1.0");
QObject::connect(&client, &opentalk::sdk::Client::connected, [](const opentalk::sdk::ServerInfo &s) {
    qInfo() << "connected to" << s.name;
});
QObject::connect(&client, &opentalk::sdk::Client::disconnected, [](const opentalk::sdk::DisconnectReason &r) {
    qInfo() << "disconnected:" << r.message;
});
client.connectToServer({"chat.example.org", 10350}, {"SHA256:AB:CD:..."});
```

A complete program is in [`examples/opentalk-ping`](../../examples/opentalk-ping/main.cpp).

## Server identity

Pass the server's certificate fingerprint as `ServerIdentity::pinnedFingerprint`. The server administrator can read it from the server's start-up output. If you leave it empty, the server must have a certificate from a public authority, for example Let's Encrypt. When a certificate is rejected, `DisconnectReason::certificateFingerprint` tells you what the server presented, so your program can ask its user to confirm it.

## Versioning and compatibility policy

- The SDK follows [Semantic Versioning](https://semver.org/). `OPENTALK_SDK_VERSION_*` in `Version.h` gives the compile-time version, and `runtimeVersion()` gives the version in use at run time.
- **0.x (now):** a draft. Any minor release may change the API, and changes are listed in the changelog.
- **1.0 and later:** source compatibility within a major version. Fields and functions are added but never removed or changed in meaning. Classes use a private implementation (pimpl) so that binary compatibility can be kept once the SDK ships as a shared library.
- The SDK version is separate from the **protocol** version. Newer SDKs keep speaking older protocol versions for at least one release cycle (see the [protocol compatibility policy](../protocol/proposal.md#compatibility-policy)).
- Only `opentalk/sdk/*` headers are public. Other OpenTalk headers are internal and may change at any time.

## Licensing

The SDK is GPL-3.0-only, like the rest of OpenTalk. Programs that link it must be distributable under GPL-3.0-compatible terms. A policy for plugins and external tools is planned for Phase 6, with legal review.
