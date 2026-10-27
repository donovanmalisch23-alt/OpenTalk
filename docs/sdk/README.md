# OpenTalk SDK

**Status: draft (SDK 0.2).** It is implemented and covered by tests through the `opentalk-ping` example. It can log in (account or guest), list channels and users, join channels, send and receive channel and private messages, reconnect automatically, and measure round-trip time. Audio is added in Phase 4.

The SDK is the supported way for bots, tools and other programs to talk to OpenTalk servers. The OpenTalk desktop application is built on the same internal libraries.

## Using it

The public headers are `include/opentalk/sdk/Client.h` and `Version.h`. Link the `OpenTalk::Sdk` CMake target. You need Qt 6 (Core and Network) and a running Qt event loop.

```cpp
#include <opentalk/sdk/Client.h>

QString error;
if (!opentalk::sdk::Client::initialize(&error)) { /* TLS 1.3 unavailable */ }

opentalk::sdk::Client client;
client.setApplication("my-bot", "1.0");
QObject::connect(&client, &opentalk::sdk::Client::connected, [&](const opentalk::sdk::ServerInfo &s) {
    qInfo() << "connected to" << s.name << "with" << client.users().size() << "users";
    client.sendChannelMessage("Hello from a bot!");
});
QObject::connect(&client, &opentalk::sdk::Client::textReceived, [](const opentalk::sdk::TextMessage &m) {
    qInfo() << m.fromName << "said" << m.text;
});
QObject::connect(&client, &opentalk::sdk::Client::disconnected, [](const opentalk::sdk::DisconnectReason &r) {
    qInfo() << "disconnected:" << r.message;
});
opentalk::sdk::Login login;
login.mode = opentalk::sdk::Login::Mode::Account; // or Guest, with just a name
login.name = "bot";
login.password = readPasswordFromSomewhereSafe();
client.connectToServer({"chat.example.org", 10350}, {"SHA256:AB:CD:..."}, login);
```

A complete program is in [`examples/opentalk-ping`](../../examples/opentalk-ping/main.cpp).

## Bots and permissions

A bot is an ordinary account (or guest) and gets exactly the permissions of its role; being a program gives it nothing extra. Create a dedicated account for each bot.

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
