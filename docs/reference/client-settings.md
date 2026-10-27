# Client settings reference

File: `<data root>/config/client-settings.json` (see [data layout](../architecture/data-layout.md)). Format version: **1**.

```json
{
  "format": "opentalk-settings",
  "schemaVersion": 1,
  "values": { "diagnostics/logLevel": "info" }
}
```

Only values that have been set are stored, and missing keys use their defaults. Hand-edited files are validated on load: invalid values fall back to the default, a warning is shown, and the original text is kept until the setting is changed in the app.

| Key | Type | Default | Valid values | Where in the UI | Effect |
|---|---|---|---|---|---|
| `general/restoreWindowLayout` | bool | `true` | | Settings → General | Restore the window size, position and pane sizes on start. |
| `accessibility/announceStatusChanges` | bool | `true` | | Settings → Accessibility | Announce status bar changes (connection state, your channel). Needs Qt 6.8 or later. |
| `accessibility/announceMessages` | bool | `true` | | Settings → Accessibility | Announce channel, private and server messages from other people. |
| `accessibility/announcePresence` | bool | `true` | | Settings → Accessibility | Announce people joining or leaving the server or your channel (combined when several happen together). |
| `diagnostics/logLevel` | string | `"info"` | `"warning"`, `"info"`, `"debug"` | Settings → Logging and Diagnostics | Minimum level written to the log. Applied immediately. |
| `localServer/name` | string | `"My OpenTalk server"` | 1–128 characters | Host a server dialog | Last used name for a hosted server. |
| `localServer/port` | int | `10350` | 1–65535 | Host a server dialog | Last used port. |
| `localServer/allowOtherComputers` | bool | `false` | | Host a server dialog | Listen on all interfaces instead of only this computer. |
| `localServer/allowGuests` | bool | `true` | | Host a server dialog | Start the hosted server with guests allowed. |
| `localServer/adminName` | string | empty | up to 32 characters | Host a server dialog | Last administrator username (the password is never stored). |
| `ui/mainWindowGeometry` | bytes (base64) | empty | Qt geometry data | — (internal) | Saved window geometry. |
| `ui/mainSplitterState` | bytes (base64) | empty | Qt splitter data | — (internal) | Saved pane sizes. |
