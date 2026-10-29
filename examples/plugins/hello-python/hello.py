# SPDX-License-Identifier: GPL-3.0-only
"""An OpenTalk plugin in Python: adds "Who is here?" to the Plugins menu.

OpenTalk starts this script and talks to it in JSON lines: one JSON object per line
on standard input (from OpenTalk) and standard output (to OpenTalk). See
docs/plugins/developer-guide.md. Only the Python standard library is needed.
"""

import json
import sys

server = ""
users = {}  # user ID -> name


def send(message):
    sys.stdout.write(json.dumps(message) + "\n")
    sys.stdout.flush()


def main():
    global server
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        message = json.loads(line)
        kind = message.get("type")
        if kind == "hello":
            send({"type": "ready", "commands": [{"id": "who", "title": "Who is here?"}]})
        elif kind == "connected":
            server = message.get("server", "")
        elif kind == "disconnected":
            users.clear()
        elif kind == "state":
            users.clear()
            for user in message.get("users", []):
                users[user["id"]] = user["name"]
        elif kind in ("userJoined", "userMoved", "userUpdated"):
            user = message["user"]
            users[user["id"]] = user["name"]
        elif kind == "userLeft":
            users.pop(message["user"]["id"], None)
        elif kind == "command" and message.get("id") == "who":
            if not server:
                send({"type": "notify", "text": "You are not connected to a server."})
            else:
                names = ", ".join(sorted(users.values(), key=str.lower))
                count = len(users)
                people = "person" if count == 1 else "people"
                send({"type": "notify", "text": f"{count} {people} on {server}: {names}."})
        elif kind == "shutdown":
            return


if __name__ == "__main__":
    main()
