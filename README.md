# OpenTalk

**Open-source, self-hosted voice and text communication, designed from the start for screen reader users.**

OpenTalk lets communities run their own voice and text chat servers, with channels, push-to-talk, moderation tools and plugins, and no central company in the middle. It is inspired by the functionality of TeamTalk 5 but is an independent project with its own protocol, SDK and code, written in C++ with Qt.

> **Status: release candidate (version 1.0.0-rc.1).** Every feature of the original plan is in it: text chat and voice work: host a server from the app or run `opentalk-server`, then log in, use channels, talk (push-to-talk, voice activation or always-on), and send channel, private and server-wide messages. Owners and moderators have roles, sanctions, reports, appeals and an audit log. Plugins and bots extend it. You can play audio files into a channel and share your computer's sound, with noise suppression, echo cancellation and voice effects. Control traffic uses TLS 1.3 and voice is encrypted too. Voice is tested automatically with simulated sound devices but **has not yet been tried on real sound hardware**. See [docs/status.md](docs/status.md) for exactly what works and what has been verified.

## Principles

- **Accessible:** fully keyboard-operable and tested with NVDA. Accessibility bugs block releases.
- **Self-hosted:** run a server on your own PC, your LAN or a VPS; the server needs no window.
- **Open:** client, server, SDK and first-party plugins are all GPL-3.0-only.
- **Secure:** encrypted connections, server-enforced permissions, no invented cryptography, and fuzz-tested parsers.
- **Fair moderation:** roles and capabilities, required reasons, temporary sanctions, appeals and a tamper-evident audit log.
- **Extensible:** plugins for bots, media playback and audio effects, with declared and enforced capabilities.
- **Portable:** a portable mode and a documented data layout. Windows comes first; Linux and macOS are planned.

## Try it (Windows)

1. Download from the **Releases** page: the portable zip (`OpenTalk-<version>-portable-windows-x64.zip`) or the installer (`OpenTalk-<version>-setup-x64.exe`, for you only, no administrator rights). The files are not code-signed yet, so Windows SmartScreen warns: choose "More info", then "Run anyway". `SHA256SUMS.txt` lets you check the download. Development builds of both are on the latest successful **CI** run in the **Actions** tab.
2. Run the installer, or unzip the portable edition anywhere and run `OpenTalk.exe`; its settings stay in the `data` folder next to it. See [getting started](docs/user-guide/getting-started.md).

To try chatting: **Server menu → Host a server on this computer**, choose a name and password for yourself (you become the server's owner), and press Enter. To join a friend's server: **Ctrl+O**.

Keyboard: **F6** cycles between panes, **Ctrl+1** to **Ctrl+4** jump to channels, users, history and message input, **Enter** on a channel joins it, **Ctrl+N** creates a channel, **Ctrl+P** sends a private message, **Alt+O** opens the Moderation menu, **Ctrl+Shift+A** opens Administration, **Alt+P** opens the Plugins menu, **Ctrl+Shift+F** plays a file into your channel, **Ctrl+Shift+C** shares your computer's sound, and **F4** opens Settings. The full list is in the [accessibility plan](docs/accessibility/plan.md#keyboard-map-implemented).

The zip also contains `opentalk-server.exe`, `opentalk-ping.exe`, the example bot `opentalk-greeter-bot.exe` and example plugins in `examples\plugins`. See the [server quick start](docs/server-administration/quick-start.md), [hosting for others](docs/server-administration/hosting.md) (including Docker and systemd), the [voice and sounds guide](docs/user-guide/voice-and-sounds.md), the [plugin guide](docs/user-guide/plugins.md) and the [SDK guide](docs/sdk/README.md).

## Build from source

See [docs/development/building.md](docs/development/building.md). In short, with Visual Studio 2022 and Qt 6.8 or later:

```bat
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

## Documentation

Start with the [documentation index](docs/README.md): requirements, architecture, protocol, accessibility, security, moderation, licensing and the [roadmap](docs/roadmap.md).

## Contributing

Contributions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) and our [Code of Conduct](CODE_OF_CONDUCT.md). Report security problems privately as described in [SECURITY.md](SECURITY.md).

## License

OpenTalk is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License, version 3 only. See [LICENSE](LICENSE). Third-party components are listed in [docs/licensing/third-party-register.md](docs/licensing/third-party-register.md).

OpenTalk is not affiliated with or endorsed by BearWare.dk or the TeamTalk project. "TeamTalk" is used only to describe compatibility of purpose.
