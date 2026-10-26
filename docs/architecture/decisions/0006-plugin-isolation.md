# ADR 0006: Two-tier plugin model, out-of-process by default

**Status:** Provisional (Phase 6)

**Context:** Plugins include bots, media players, dashboards and audio effects. An in-process C++ library has the application's full privileges, so a manifest's permission list cannot be enforced against it.

**Decision:**

- **Out-of-process plugins (default, all third-party plugins):** run in an `opentalk-plugin-host` process and talk to the app over a local, authenticated IPC channel using the same framing as the protocol. The app enforces the capabilities granted by the user, such as "read channel messages" or "play media into channel". This is real enforcement at the IPC boundary. It is not an OS sandbox; restricted tokens or AppContainer could be added later.
- **In-process plugins (first-party only, initially):** for audio effects that must run inside the real-time chain. They are clearly labelled "full trust" in the UI.
- **Bots** are clients with their own server accounts and roles. The server authorizes them like any user, and audit entries name them.

**Consequences:** IPC adds latency, so in-process is reserved for real-time DSP. The plugin SDK has two flavours.
