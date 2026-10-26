# ADR 0003: Client settings as schema-validated JSON

**Status:** Accepted

**Context:** Settings must be portable, readable, validated, migratable, crash-safe, and never silently destroyed.

**Options:** `QSettings` (the registry or INI on Windows); JSON with a schema; SQLite.

**Decision:** A JSON file per settings domain, described by a `SettingsSchema` (key, type, default, validation rule) and managed by `SettingsStore`.

**Why not QSettings:** By default it uses the registry on Windows, which is not portable. Its INI format has no types, no validation and no versioning.

**Behaviour (all unit-tested):**

- Saves are atomic (`QSaveFile`).
- Invalid values fall back to defaults with a warning. They are kept on disk until the user changes that setting.
- Unknown keys, for example from a newer version, are preserved.
- A damaged file is moved aside and never deleted.
- An older format is backed up before migration. A failed or missing migration makes the store read-only.
- A newer format is left untouched (read-only).

**Consequences:** Every new setting needs a schema entry and a line in the [settings reference](../../reference/client-settings.md). The server will use SQLite for relational data (accounts, permissions, audit), and that will have its own ADR.
