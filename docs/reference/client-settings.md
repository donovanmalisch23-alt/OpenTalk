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
| `accessibility/announceStatusChanges` | bool | `true` | | Settings → Accessibility | Screen reader announcements for status bar changes. Needs Qt 6.8 or later; has no effect on builds with older Qt. |
| `diagnostics/logLevel` | string | `"info"` | `"warning"`, `"info"`, `"debug"` | Settings → Logging and Diagnostics | Minimum level written to the log. Applied immediately. |
| `ui/mainWindowGeometry` | bytes (base64) | empty | Qt geometry data | — (internal) | Saved window geometry. |
| `ui/mainSplitterState` | bytes (base64) | empty | Qt splitter data | — (internal) | Saved pane sizes. |
