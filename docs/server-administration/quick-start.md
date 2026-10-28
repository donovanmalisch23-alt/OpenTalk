# Server quick start

**Status:** works for real use on a local network or the internet. People can log in, use channels, talk, and send channel, private and server-wide messages. Owners, administrators and moderators have roles, sanctions, reports, appeals and an audit log ([moderation guide](moderation.md)).

There are two ways to run a server:

- **From the app:** Server menu → **Host a server on this computer**. The app runs `opentalk-server` for you, creates your account as the server's owner, and joins it. Your server stops when you choose Stop hosting or close OpenTalk.
- **On its own**, for a server that keeps running without the app (a spare PC, a dedicated machine or a VPS), as described below.

## 1. Get the programs

Download **OpenTalk-portable-windows-x64** from the latest successful CI run (repository → Actions). It contains `opentalk-server.exe` and the `opentalk-ping.exe` test tool.

## 2. Create the owner's account

```bat
opentalk-server --data-dir C:\OpenTalkServer --create-account yourname --role owner
```

It asks for the password twice (at least 8 characters) without showing it. A server has one owner. Add more accounts the same way with `--role admin`, `--role moderator` or `--role user`, or later from the app (Moderation → Administration → Accounts). Other account commands:

| Command | What it does |
|---|---|
| `--list-accounts` | Lists accounts and their roles. |
| `--create-account <name> --bot` | Creates an account for a bot (a program). Everyone sees it marked as a bot. |
| `--set-password <name>` | Changes a password. |
| `--delete-account <name>` | Deletes an account. |
| `--recover-owner <name>` | Makes an existing account the owner, if the owner is locked out. Stop the server first. |
| `--verify-audit` | Checks that the audit log was not changed (exit code 2 if it was). |
| `--export-audit <file>` | Writes the audit log to a text file. |

The account commands work while the server is running; `--recover-owner` needs it stopped.

## 3. Start the server

```bat
opentalk-server --data-dir C:\OpenTalkServer --name "My Server" --allow-guests
```

The first time, it creates a self-signed **certificate** in the data folder. It prints the port and the certificate **fingerprint**. Send the fingerprint to your users through a channel they trust (for example in person or a private message), so they can check it when OpenTalk asks them to the first time.

| Option | Default | Meaning |
|---|---|---|
| `--data-dir` | per-user application data folder | Where the database (`opentalk.db`) and certificate (`tls\`) are kept. Back this folder up. |
| `--port` | 10350 | TCP port. `0` picks any free port. |
| `--listen` | all interfaces | IP address to listen on, for example `127.0.0.1` for this computer only. |
| `--name` | OpenTalk server | Name shown to users (1–128 characters). |
| `--allow-guests` | off | Let people join with just a nickname. Guests can chat, talk and join channels but cannot create channels. |
| `--no-udp` | UDP on | Carry voice only over the TCP connections. Use it only when the UDP port cannot be opened; voice then has a little more delay. |
| `--max-users` | 200 | Maximum simultaneous connections. |
| `--tls-cert`, `--tls-key` | `<data>\tls\server.crt` and `.key` | Use your own certificate, for example from Let's Encrypt (give the full-chain file). |
| `--log-level` | info | `warning`, `info` or `debug`. `debug` includes IP addresses. |
| `--audit-retention-days` | 365 | How long the audit log keeps entries. `0` keeps them forever. |

Once someone changes the server settings from the app (Moderation → Administration → Server settings), the saved name, guest setting and user limit take precedence over `--name`, `--allow-guests` and `--max-users`.

Press **Ctrl+C** to stop. Connected users are told the server is shutting down.

## 4. Roles

| Role | Can |
|---|---|
| Guest | Join channels, chat, send private messages, talk |
| Registered user | The same, plus create temporary channels (they vanish when empty) |
| Moderator | Warn, mute, kick, temporarily ban and move people; handle reports and appeals; read the audit log |
| Administrator | Everything except transferring ownership: persistent channels, accounts, roles, permissions, server settings |
| Owner | Everything, including transferring ownership |

Owners and administrators can change these and add their own roles and per-channel rules. See the [moderation guide](moderation.md).

## 5. Check it from another computer

```bat
opentalk-ping my-server.example.org --fingerprint SHA256:AB:CD:...
```

It logs in as a guest (or use `--user name` and type the password), and prints the server name, the number of users and channels, and the round-trip time.

## 6. Reaching the server from the internet

- Allow **TCP and UDP port 10350** (or your chosen port) through the firewall on the server machine. Windows Defender Firewall asks the first time the server starts; allow it for the networks you use.
- On a home network, forward that port for **both TCP and UDP** on your router to the server computer.
- Some internet providers use carrier-grade NAT, which makes incoming connections impossible without a VPS or a VPN. Ask your provider, or check whether your router's WAN address matches what websites report as your public IP.
- **If only TCP gets through,** voice still works: clients notice that UDP is blocked and send voice over their TCP connection instead, with a little more delay. The history in the app says so when this happens.

## Security and privacy notes

- Connections use TLS 1.3 only. Older clients and unencrypted connections are refused.
- Passwords are stored only as Argon2id hashes. Repeated failed logins from one address are blocked for 10 minutes.
- **Text messages are not stored on the server.** It passes them on and keeps nothing.
- **Voice is encrypted** between each client and the server (XChaCha20-Poly1305 over UDP with fresh keys per login, or TLS when using TCP). The server forwards voice without storing or decoding it. Voice is **not end-to-end encrypted**: whoever runs the server could, with modified software, listen in. This is the same trust model as TeamTalk and Mumble.
- Each person can send at most 200 voice packets per second; extra packets are dropped.
- Each IP address may open at most 10 connections. Silent connections are dropped after 10 seconds, and flooding clients are disconnected or slowed down.
- Logs at the default level contain no IP addresses, passwords or message text.
