# Server quick start

**Status:** works for real use on a local network or the internet. People can log in, use channels, and send channel, private and server-wide messages. Voice arrives in Phase 4, and fuller moderation tools in Phase 5.

There are two ways to run a server:

- **From the app:** Server menu → **Host a server on this computer**. The app runs `opentalk-server` for you, creates your administrator account, and joins it. Your server stops when you choose Stop hosting or close OpenTalk.
- **On its own**, for a server that keeps running without the app (a spare PC, a dedicated machine or a VPS), as described below.

## 1. Get the programs

Download **OpenTalk-portable-windows-x64** from the latest successful CI run (repository → Actions). It contains `opentalk-server.exe` and the `opentalk-ping.exe` test tool.

## 2. Create an administrator account

```bat
opentalk-server --data-dir C:\OpenTalkServer --create-account yourname --role admin
```

It asks for the password twice (at least 8 characters) without showing it. Add more accounts the same way with `--role user`. Other account commands:

| Command | What it does |
|---|---|
| `--list-accounts` | Lists accounts and their roles. |
| `--set-password <name>` | Changes a password. |
| `--delete-account <name>` | Deletes an account. |

These commands work while the server is running.

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
| `--allow-guests` | off | Let people join with just a nickname. Guests can chat and join channels but cannot create channels. |
| `--max-users` | 200 | Maximum simultaneous connections. |
| `--tls-cert`, `--tls-key` | `<data>\tls\server.crt` and `.key` | Use your own certificate, for example from Let's Encrypt (give the full-chain file). |
| `--log-level` | info | `warning`, `info` or `debug`. `debug` includes IP addresses. |

Press **Ctrl+C** to stop. Connected users are told the server is shutting down.

## 4. Roles

| Role | Can |
|---|---|
| Guest | Join channels, chat, send private messages |
| User | The same, plus create temporary channels (they vanish when empty) |
| Admin | Everything: persistent channels, channel passwords and limits, deleting channels, server-wide messages; not blocked by channel passwords or limits |

## 5. Check it from another computer

```bat
opentalk-ping my-server.example.org --fingerprint SHA256:AB:CD:...
```

It logs in as a guest (or use `--user name` and type the password), and prints the server name, the number of users and channels, and the round-trip time.

## 6. Reaching the server from the internet

- Allow **TCP port 10350** (or your chosen port) through the firewall on the server machine. Windows Defender Firewall asks the first time the server starts.
- On a home network, forward that TCP port on your router to the server computer.
- Some internet providers use carrier-grade NAT, which makes incoming connections impossible without a VPS or a VPN. Ask your provider, or check whether your router's WAN address matches what websites report as your public IP.
- Voice will also need a **UDP** port (the same number by default) when audio arrives in Phase 4.

## Security and privacy notes

- Connections use TLS 1.3 only. Older clients and unencrypted connections are refused.
- Passwords are stored only as Argon2id hashes. Repeated failed logins from one address are blocked for 10 minutes.
- **Text messages are not stored on the server.** It passes them on and keeps nothing.
- Each IP address may open at most 10 connections. Silent connections are dropped after 10 seconds, and flooding clients are disconnected or slowed down.
- Logs at the default level contain no IP addresses, passwords or message text.
