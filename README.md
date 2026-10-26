# OpenTalk

**Open-source, self-hosted voice and text communication, designed from the start for screen reader users.**

OpenTalk lets communities run their own voice and text chat servers, with channels, push-to-talk, moderation tools and plugins, and no central company in the middle. It is inspired by the functionality of TeamTalk 5 but is an independent project with its own protocol, SDK and code, written in C++ with Qt.

> **Status: early development (version 0.1.0).** The desktop window, keyboard navigation, settings and the protocol's base layer exist and are tested. Connecting to servers, voice and text chat are **not implemented yet**. See [docs/status.md](docs/status.md) for exactly what works and what has been verified.

## Principles

- **Accessible:** fully keyboard-operable and tested with NVDA. Accessibility bugs block releases.
- **Self-hosted:** run a server on your own PC, your LAN or a VPS; the server needs no window.
- **Open:** client, server, SDK and first-party plugins are all GPL-3.0-only.
- **Secure:** encrypted connections, server-enforced permissions, no invented cryptography, and fuzz-tested parsers.
- **Fair moderation:** roles and capabilities, required reasons, temporary sanctions, appeals and a tamper-evident audit log.
- **Extensible:** plugins for bots, media playback and audio effects, with declared and enforced capabilities.
- **Portable:** a portable mode and a documented data layout. Windows comes first; Linux and macOS are planned.

## Try the current build (Windows)

1. Open the repository's **Actions** tab, select the latest successful **CI** run, and download **OpenTalk-portable-windows-x64**.
2. Unzip it anywhere and run `OpenTalk.exe`. No installation or administrator rights are needed; settings stay in the `data` folder next to it.

Keyboard: **F6** cycles between panes, **Ctrl+1** to **Ctrl+4** jump to channels, users, history and message input, and **F4** opens Settings.

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
