# ADR 0004: Server as a library with a headless host process

**Status:** Accepted

**Context:** The desktop app must start and manage a local server, but a server must never need a GUI, and must run on dedicated machines and VPSs.

**Options:**

1. Server inside the GUI process (threads).
2. Server library plus a headless `opentalk-server` executable. The GUI launches it as a child process and manages it through the protocol.
3. Always a Windows service.

**Decision:** Option 2.

**Why:** It isolates failures (a crash in the GUI or a plugin does not drop every connected user), the same binary is used locally and remotely, and administration has one code path. Option 1 is simpler but couples crashes and lifetimes. Option 3 needs administrator rights for installation, which contradicts portable, no-admin use. Service mode can be added on top of option 2 later.

**Consequences:** Two executables ship. The GUI needs a supervisor component (start, stop, health, logs) and a local admin credential, generated on first start and stored with user-only file permissions.
