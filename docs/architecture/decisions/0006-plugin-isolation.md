# ADR 0006: Out-of-process plugins with enforced capabilities

**Status:** Accepted (Phase 6), revising the provisional decision of Phase 0.

**Context:** Plugins include bots, media players, dashboards and audio effects. An in-process C++ library has the application's full privileges, so a manifest's permission list cannot be enforced against it. Plugin authors also need to be able to write plugins in languages other than C++.

**Decision:**

- **Every plugin is its own process**, started by the app with its working directory set to the plugin's folder. (Phase 0 proposed one shared `opentalk-plugin-host` process that would load plugins; a process per plugin is simpler, isolates plugins from each other as well as from the app, and lets a plugin be a program in any language.)
- **IPC is JSON lines over the plugin's standard input and output** ([reference](../../plugins/ipc.md)). Phase 0 proposed the protocol's own CBOR framing; JSON lines can be produced and parsed by every language's standard library, which matters more for third-party authors than the small cost of JSON. The pipes are created by the app for that one child process, so nothing else can connect: no further authentication is needed.
- **Capabilities are enforced in the app** at the IPC boundary (`PluginHost`): a plugin receives only the events it was granted and its requests are refused unless granted. The user grants them in a consent dialog, can untick any, and must review again whenever `plugin.json` changes (its SHA-256 is recorded with the consent).
- **Transport limits** protect the app: line length, message rate, a handshake timeout, a count of broken rules, and a restart limit for crashing plugins. A plugin that breaks them is turned off and the user is told why.
- **No OS sandbox.** A plugin runs with the user's rights. This is stated in the consent dialog and the developer guide. Restricted tokens or AppContainer can be added later without changing the plugin interface.
- **In-process plugins are not supported.** Real-time audio effects (Phase 7) are built into OpenTalk instead, so no third-party code runs inside the app.
- **Bots** are clients with their own server accounts and roles, written with the SDK. Accounts can be marked as bots (`--bot`); everyone sees the mark and the audit log names bots as such. The server authorizes them like any user.

**Consequences:**

- A plugin cannot crash or freeze the app, and a misbehaving plugin is stopped automatically.
- Each plugin costs a process. That is acceptable for the expected handful of plugins.
- Plugins cannot do real-time audio processing; audio streaming by plugins is planned with media streaming (Phase 7) through the same message interface.
- The licensing position for plugins rests on their being separate programs ([licensing policy](../../plugins/licensing.md)), which needs legal review.
