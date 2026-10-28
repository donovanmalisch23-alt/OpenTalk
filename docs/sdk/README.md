# OpenTalk SDK

**Status: draft (SDK 0.3).** It is implemented and covered by tests through two examples run against a real server: `opentalk-ping` (`tst_server_process`) and `opentalk-greeter-bot` (`tst_examples`). It can log in (account or guest), list channels and users (with their role, mutes and whether they are bots), join channels, send and receive channel, private and server-wide messages, send and receive audio, reconnect automatically, and measure round-trip time.

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

Complete programs: [`examples/opentalk-ping`](../../examples/opentalk-ping/main.cpp) (checks a server) and [`examples/greeter-bot`](../../examples/greeter-bot/main.cpp) (a bot that greets people, answers `!help`, `!time`, `!roll`, `!who` and `!beep`, and plays a tone).

## Audio (since 0.3)

Audio travels as Opus packets of 20 ms. `AudioEncoder` turns 960 samples of 48 kHz mono audio (floats from -1 to 1) into a packet; `AudioDecoder` does the reverse, one decoder per speaker.

```cpp
opentalk::sdk::AudioEncoder encoder(opentalk::sdk::AudioEncoder::Content::Music, 64000);
QTimer timer;
timer.setInterval(20);
QObject::connect(&timer, &QTimer::timeout, [&] {
    QList<float> frame = nextTwentyMilliseconds(); // 960 samples
    client.sendAudio(encoder.encode(frame), opentalk::sdk::AudioStream::Media);
});
QObject::connect(&client, &opentalk::sdk::Client::audioReady, [&] { timer.start(); });
```

`sendAudio()` works once `audioReady()` has been emitted (after each login). Received audio arrives through `audioReceived(userId, stream, packet)`. The server forwards audio only to the sender's channel, and only if the sender's role may transmit there.

## Bots

A bot is an ordinary account and gets exactly the permissions of its role; being a program gives it nothing extra.

- Create a dedicated account for each bot, marked as a bot: `opentalk-server --create-account greeter --bot` (or tick "This account is for a bot" in Moderation, Administration, Accounts). Everyone sees "bot" next to its name, and the audit log marks what it does.
- Give it the smallest role that works. A moderation bot needs a moderator role; a greeter needs nothing special.
- `User::bot` tells your bot which users are bots. **Never answer other bots automatically**, or two bots can keep each other talking.
- Keep the password out of the command line (other users of the computer can see command lines). The example reads it from standard input.

## Server identity

Pass the server's certificate fingerprint as `ServerIdentity::pinnedFingerprint`. The server administrator can read it from the server's start-up output. If you leave it empty, the server must have a certificate from a public authority, for example Let's Encrypt. When a certificate is rejected, `DisconnectReason::certificateFingerprint` tells you what the server presented, so your program can ask its user to confirm it.

## Versioning and compatibility policy

- The SDK follows [Semantic Versioning](https://semver.org/). `OPENTALK_SDK_VERSION_*` in `Version.h` gives the compile-time version, and `runtimeVersion()` gives the version in use at run time.
- **0.x (now):** a draft. Any minor release may change the API, and changes are listed in the changelog.
- **1.0 and later:** source compatibility within a major version. Fields and functions are added but never removed or changed in meaning. Classes use a private implementation (pimpl) so that binary compatibility can be kept once the SDK ships as a shared library.
- The SDK version is separate from the **protocol** version. Newer SDKs keep speaking older protocol versions for at least one release cycle (see the [protocol compatibility policy](../protocol/proposal.md#compatibility-policy)).
- Only `opentalk/sdk/*` headers are public. Other OpenTalk headers are internal and may change at any time.

## Licensing

The SDK is GPL-3.0-only, like the rest of OpenTalk. Programs that link it must be distributable under GPL-3.0-compatible terms. See the [licensing policy for plugins, bots and other programs](../plugins/licensing.md) (a draft awaiting legal review).
