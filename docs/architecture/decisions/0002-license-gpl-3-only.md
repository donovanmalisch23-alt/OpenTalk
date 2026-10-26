# ADR 0002: GPL-3.0-only for all first-party code

**Status:** Accepted (owner requirement)

**Decision:** The client, server, SDK, first-party plugins, tools, tests and build scripts are GPL-3.0-only. Each source file carries `SPDX-License-Identifier: GPL-3.0-only`. Documentation is under the same license unless a file says otherwise.

**Consequences:**

- Dependencies must be GPL-3.0-compatible: permissive licenses, LGPL, and GPLv3-or-later are compatible; **GPLv2-only is not**. See [policy](../../licensing/policy.md).
- Third-party plugins that link the SDK in-process are probably derivative works and would have to be GPL-compatible. Out-of-process plugins that speak the documented IPC protocol are less clearly covered. A written plugin licensing policy needs legal review before Phase 6. This is an open question and no conclusion is claimed here.
- Contributions are accepted under the same license using the Developer Certificate of Origin (`Signed-off-by`) rather than a CLA. That keeps relicensing to anything other than GPL-3.0-only impractical, which matches the project's intent.
