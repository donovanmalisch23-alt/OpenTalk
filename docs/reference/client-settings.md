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
| `accessibility/announceTalking` | bool | `false` | | Settings → Accessibility | Speak "name talking" when someone in your channel starts talking. The channel list always shows it (bold, with the description "talking"). |
| `audio/inputDevice` | string | empty | device name, empty = system default | Settings → Audio | Microphone. A device that is not plugged in stays selected and is used when it comes back. |
| `audio/outputDevice` | string | empty | device name, empty = system default | Settings → Audio | Speakers or headphones for voices and sounds. |
| `audio/inputVolume` | int | `100` | 0–400 (percent) | Settings → Audio | Microphone gain before sending. |
| `audio/outputVolume` | int | `100` | 0–400 (percent) | Settings → Audio | Volume of everyone you hear. |
| `audio/transmitMode` | string | `"pushToTalk"` | `"pushToTalk"`, `"voiceActivation"`, `"continuous"` | Settings → Audio | How talking starts. Ctrl+T turns talking on and off in push-to-talk mode. |
| `audio/pushToTalkKey` | string | empty (none) | one key combination, Qt portable text such as `"F12"` or `"Ctrl+Meta"` | Settings → Audio → Change key | Key to hold while talking. Global on Windows; in OpenTalk's window elsewhere. |
| `audio/voiceActivationLevel` | int | `-45` | -80–0 (dBFS) | Settings → Audio | Microphone level that starts transmission in voice activation mode. |
| `audio/bitrateKbps` | int | `32` | 8–128 | Settings → Audio → Voice quality | Opus bit rate; capped by the server's limit. |
| `audio/useUdp` | bool | `true` | | Settings → Audio | Use encrypted UDP for voice when it works (otherwise voice uses the main connection). Applies from the next connection. |
| `audio/userVolumes` | object of int | `{}` | values 0–400 (percent); keys `"host:port/user name"` | Channel menu → Volume for selected user (Ctrl+Shift+V) | Per-person volume, remembered per server and user name. 100 is not stored. |
| `sounds/enabled` | bool | `true` | | Settings → Sounds | Play event sounds. |
| `sounds/volume` | int | `80` | 0–100 (percent) | Settings → Sounds | Event sound volume. |
| `sounds/pushToTalk` | bool | `true` | | Settings → Sounds | Click when talking starts and stops. |
| `sounds/pack` | string | empty (built-in) | folder name, no slashes | Settings → Sounds | Soundpack folder inside `<data root>/soundpacks` or next to the program. |
| `diagnostics/logLevel` | string | `"info"` | `"warning"`, `"info"`, `"debug"` | Settings → Logging and Diagnostics | Minimum level written to the log. Applied immediately. |
| `localServer/name` | string | `"My OpenTalk server"` | 1–128 characters | Host a server dialog | Last used name for a hosted server. |
| `localServer/port` | int | `10350` | 1–65535 | Host a server dialog | Last used port. |
| `localServer/allowOtherComputers` | bool | `false` | | Host a server dialog | Listen on all interfaces instead of only this computer. |
| `localServer/allowGuests` | bool | `true` | | Host a server dialog | Start the hosted server with guests allowed. |
| `localServer/adminName` | string | empty | up to 32 characters | Host a server dialog | Last administrator username (the password is never stored). |
| `ui/mainWindowGeometry` | bytes (base64) | empty | Qt geometry data | — (internal) | Saved window geometry. |
| `ui/mainSplitterState` | bytes (base64) | empty | Qt splitter data | — (internal) | Saved pane sizes. |
