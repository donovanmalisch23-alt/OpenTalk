# Writing an OpenTalk plugin

**Status:** plugin API version 1, implemented in Phase 6. The examples below are built and tested in CI (`tst_plugins`).

A plugin adds features to the OpenTalk app: a menu command, a dice roller, a reminder service, a bridge to another chat system. It is **a separate program** that OpenTalk starts and talks to over its standard input and output. You can write it in any language that can read and write lines of text. OpenTalk only lets it see and do what the user allowed.

If you want a program that is a participant in its own right (with its own name in the channel list, running on a server without anyone's app open), write a **bot** with the [SDK](../sdk/README.md) instead.

## A plugin's folder

```
org.example.hello/
├── plugin.json      what it is, what to run and what it needs
├── hello.py         the program (or hello.exe, a folder of files, ...)
└── ...
```

### `plugin.json`

```json
{
    "id": "org.example.hello",
    "name": "Hello",
    "version": "1.0.0",
    "description": "Says hello when you choose Plugins, Say hello.",
    "author": "Your name",
    "license": "MIT",
    "homepage": "https://example.org/hello",
    "apiVersion": 1,
    "interpreter": "python3",
    "executable": "hello.py",
    "arguments": [],
    "capabilities": ["notifications.show", "commands.add"]
}
```

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | Lower-case reverse domain name, unique to your plugin. The installed folder is named after it. |
| `name`, `version` | yes | Shown to the user. `version` uses digits, letters, `.`, `+` and `-`. |
| `description`, `author`, `license`, `homepage` | no | Shown in the consent dialog and plugin manager. Use an [SPDX identifier](https://spdx.org/licenses/) for `license`. |
| `apiVersion` | no (1) | The plugin API version you wrote it for. OpenTalk refuses newer versions it does not understand. |
| `executable` | yes | The program, relative to the folder. Either one path, or one per system: `{"windows": "hello.exe", "linux": "hello", "macos": "hello"}`. It must be inside the folder (no `..`, no absolute paths, no links pointing outside). |
| `interpreter` | no | `python3`, `python` or `node`, found on the user's system. `executable` is then the script. |
| `arguments` | no | Up to 20 command-line arguments. |
| `capabilities` | no | What it needs (below). Ask only for what you use: users see the list. |

**Any change to `plugin.json` makes the user review the plugin again** before it runs, so an update cannot quietly gain permissions.

## Capabilities

| Name | Lets the plugin | Shown as sensitive |
|---|---|---|
| `presence.read` | See the channels and who is in them | |
| `messages.read` | Read the messages in your channel and messages to everyone | yes |
| `messages.private` | Read your private messages, sent and received | yes |
| `messages.send` | Send messages in your name | yes |
| `channels.join` | Move you to another channel | yes |
| `notifications.show` | Add lines to your history and have your screen reader speak them | |
| `commands.add` | Add items to the Plugins menu | |

The user can untick any of them when turning the plugin on. The `hello` message says what was actually granted, so check `granted` and degrade gracefully.

## The conversation

Read lines from standard input and write lines to standard output, **flushing after each line**. Every line is one JSON object with a `type`. The full list is in the [message reference](ipc.md).

1. OpenTalk sends `hello`. Answer with `ready` within 10 seconds, listing your menu commands if you have any.
2. You receive `connected` when the user is logged in to a server (and `disconnected` when not), events you are allowed to see, and `command` when the user chooses one of your menu items.
3. Send requests (`notify`, `sendMessage`, `joinChannel`, `getState`). Give a `requestId` to be told the outcome in a `result`.
4. On `shutdown`, or when standard input closes, exit promptly.

Write diagnostics to standard error, or send `{"type": "log", "text": "..."}`: both end up in the plugin's log, which the user can open from the plugin manager.

### Example: Python

The complete example is [`examples/plugins/hello-python`](../../examples/plugins/hello-python). Its core:

```python
import json, sys

def send(message):
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()

for line in sys.stdin:
    message = json.loads(line)
    if message["type"] == "hello":
        send({"type": "ready", "commands": [{"id": "who", "title": "Who is here?"}]})
    elif message["type"] == "command" and message["id"] == "who":
        send({"type": "notify", "text": "Hello from Python!"})
    elif message["type"] == "shutdown":
        break
```

### Example: C++

[`examples/plugins/dice`](../../examples/plugins/dice) is a dice roller using Qt's JSON classes and nothing from OpenTalk. It answers its menu command and `!dice` typed in the channel. On Windows, OpenTalk's folder is on the plugin's `PATH`, so a plugin built with the same Qt version can use the Qt libraries that come with OpenTalk; otherwise ship the libraries it needs in its folder.

## Rules your plugin must follow

- **One JSON object per line**, at most 64 KiB.
- **At most 20 messages a second** (bursts of 40). Notifications: 2 a second. Messages sent in the user's name: 1 a second.
- **Do not answer yourself.** Messages you send come back as `message` with `fromSelf: true`.
- **Do not hang on start-up.** Send `ready` before doing slow work.
- Breaking the rules repeatedly (invalid JSON, requests you were not granted, flooding) gets the plugin turned off, and the user is told why. So does crashing four times in a minute.

## Testing your plugin

- Run it by hand and type the JSON lines yourself: `{"type":"hello","apiVersion":1,"granted":["notifications.show"]}` then `{"type":"command","id":"who"}`.
- Install it in OpenTalk: **Plugins → Manage plugins → Install from folder**, choose its folder, then **Turn on**. Reinstalling the same folder updates it.
- Start OpenTalk with `--data-dir C:\temp\plugin-test` to test in a separate profile, and with `--no-plugins` if a plugin stops OpenTalk from working normally.
- Read its log from the plugin manager (**Show log**).

## What plugins cannot do (yet)

- Play or capture audio (planned with media streaming in Phase 7).
- Moderate (use a bot account with the right role instead).
- Store settings through OpenTalk: write your own files in `OPENTALK_PLUGIN_DATA`.
- Show their own windows through OpenTalk. A plugin can open its own windows as any program can, but must keep them accessible.

## Security model, honestly

The capability checks are real: OpenTalk does not send a plugin anything it was not granted, and refuses requests it was not granted. But a plugin is an ordinary program running with the user's rights. OpenTalk does **not** sandbox it: a malicious plugin could read the user's files or install malware, as any downloaded program could. The consent dialog says so. See [ADR 0006](../architecture/decisions/0006-plugin-isolation.md) and the [licensing policy](licensing.md).
