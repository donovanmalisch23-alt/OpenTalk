# Plugin messages (API version 1)

**Status:** implemented in Phase 6 and tested with real plugin processes (`tst_plugins`, `tst_plugin_ui`). This is the reference; the [developer guide](developer-guide.md) explains how to use it.

## Transport

- OpenTalk starts the plugin's program with its **working directory** set to the plugin's folder, and talks to it over **standard input and output**: one JSON object per line, UTF-8, ending with a line break (`\n`; `\r\n` is accepted).
- Anything the plugin writes to **standard error** goes to its log, `<data>/logs/plugins/<id>.log` (rotated at 1 MiB). Users can read it from the plugin manager.
- Environment variables: `OPENTALK_PLUGIN_ID`, `OPENTALK_PLUGIN_DATA` (a folder the plugin may write to; it is kept when the plugin is updated) and `OPENTALK_PLUGIN_API` (`1`). On Windows, OpenTalk's own folder is appended to `PATH`.
- Unknown fields must be ignored by both sides, so later versions can add fields.

| Rule | Limit | When broken |
|---|---|---|
| Handshake | Answer `hello` with `ready` within 10 seconds | Turned off |
| Line length | 64 KiB | Turned off at once |
| Message rate | 20 messages a second, bursts of 40 | Extra messages are dropped and count as broken rules |
| Broken rules | Invalid JSON, unknown types, messages it was not granted, invalid fields | After 20, turned off |
| Unexpected exit | Restarted up to 3 times, after 1, 2 and 4 seconds | A 4th exit within a minute turns it off |
| Shutdown | Exit within 3 seconds of `shutdown` | Ended |

"Turned off" means the plugin is stopped, the user is told why (in the history and the plugin manager), and it stays off until the user restarts it in the plugin manager or starts OpenTalk again.

## From OpenTalk to the plugin

| Type | When | Needs | Fields |
|---|---|---|---|
| `hello` | First message after start | — | `apiVersion`, `app` ("OpenTalk"), `appVersion`, `pluginId`, `granted` (capability names), `dataDir` |
| `connected` | After `ready` if connected, and after each login | — | `server` (name), `self` (`id`, `name`) |
| `disconnected` | The connection ended or was lost | — | — |
| `state` | After `connected`, and in answer to `getState` | `presence.read` | `channels`, `users`, `requestId` (when answering) |
| `channelAdded`, `channelUpdated`, `channelRemoved` | Channels change | `presence.read` | `channel` |
| `userJoined`, `userLeft`, `userMoved`, `userUpdated` | People arrive, leave, move, or their role, mute or bot status changes | `presence.read` | `user` |
| `message` | A message in your channel or to everyone | `messages.read` | `kind` (`channel` or `server`), `from` (`id`, `name`), `fromSelf`, `text`, `time` (ISO 8601, UTC), `channelId` |
| `message` | A private message to or from you | `messages.private` | `kind` (`private`), `from`, `fromSelf`, `text`, `time`, `toUserId` |
| `command` | The user chose one of the plugin's menu items | `commands.add` | `id` |
| `result` | Answer to a request that had a `requestId` | — | `requestId`, `ok`, `error` (when not ok) |
| `shutdown` | OpenTalk is closing or the plugin is being turned off | — | — |

**Channel:** `id`, `parentId` (0 for the top), `name` (the server's name for the top channel), `topic`, `persistent`, `hasPassword`, `maxUsers` (0 = no limit).

**User:** `id` (valid while they are connected), `name`, `channelId`, `role` (the role's name, for example "Moderator"), `self`, `bot`, `voiceMuted`, `textMuted`.

Messages you send come back to you as `message` with `fromSelf: true`. Ignore those when you react to commands, or the plugin will answer itself.

## From the plugin to OpenTalk

Any request may carry a `requestId` (a number or text). If it does, OpenTalk answers with a `result` carrying the same `requestId`: at once if the request is refused, or when the server has answered.

| Type | Needs | Fields | Limits |
|---|---|---|---|
| `ready` | — | `commands` (optional): up to 20 of `{id, title}`. IDs are 1–40 of `A–Z a–z 0–9 . _ -`; titles 1–60 visible characters. Commands need `commands.add`. | Once, within 10 s of `hello` |
| `log` | — | `text` (up to 2,000 characters are kept) | — |
| `notify` | `notifications.show` | `text` (1–500 visible characters), `speak` (default `true`: the screen reader reads it) | 2 a second, bursts of 5 |
| `sendMessage` | `messages.send` | `to` (`channel`: your current channel; `user`: a private message), `userId` (for `user`), `text` | 1 a second, bursts of 5; the server's own chat limits also apply |
| `joinChannel` | `channels.join` | `channelId`, `password` (optional) | — |
| `getState` | `presence.read` | — | Answered by `state` |

A request the plugin was not granted is answered with `ok: false` and an error such as `not allowed: messages.send`, and counts as a broken rule.

## Example session

```
OpenTalk → {"type":"hello","apiVersion":1,"app":"OpenTalk","appVersion":"0.1.0","pluginId":"org.example.dice","granted":["messages.send","commands.add"],"dataDir":"C:\\...\\plugin-data\\org.example.dice"}
plugin   → {"type":"ready","commands":[{"id":"roll","title":"Roll a dice"}]}
OpenTalk → {"type":"connected","server":"Friends","self":{"id":12,"name":"alice"}}
OpenTalk → {"type":"command","id":"roll"}
plugin   → {"type":"sendMessage","requestId":1,"to":"channel","text":"alice rolled 4 (1 to 6)."}
OpenTalk → {"type":"result","requestId":1,"ok":true}
OpenTalk → {"type":"shutdown"}
```
