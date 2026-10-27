# Server quick start

**Status:** works today for testing. `opentalk-server` accepts encrypted connections and handles the protocol handshake. Accounts, channels, chat and voice are not implemented yet, so there is nothing to *do* on a server yet except connect.

## 1. Get the programs

Download **OpenTalk-portable-windows-x64** from the latest successful CI run (repository → Actions). It contains `opentalk-server.exe` and the `opentalk-ping.exe` test tool.

## 2. Create a certificate

The server needs a TLS certificate and private key. For a private server, a self-signed certificate is fine: users confirm its fingerprint once, and OpenTalk then remembers it. With OpenSSL installed (it is included with Git for Windows):

```sh
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -days 3650 \
  -keyout server.key -out server.crt -subj "/CN=my-opentalk-server"
```

Keep `server.key` private. Anyone with it can impersonate your server. A future version will create a certificate automatically on first start.

If your server has a domain name, you can use a certificate from a public authority such as Let's Encrypt instead. Use the full-chain file as `--tls-cert`.

## 3. Start the server

```bat
opentalk-server --tls-cert server.crt --tls-key server.key --name "My Server"
```

It prints the port and the **certificate fingerprint**. Send the fingerprint to your users through a channel they trust (for example in person or a private message), so they can check it the first time they connect.

| Option | Default | Meaning |
|---|---|---|
| `--port` | 10350 | TCP port. `0` picks any free port. |
| `--listen` | all interfaces | IP address to listen on, for example `127.0.0.1` for this computer only. |
| `--name` | OpenTalk server | Name shown to users (1–128 characters). |
| `--max-users` | 200 | Maximum simultaneous connections. |
| `--log-level` | info | `warning`, `info` or `debug`. `debug` includes IP addresses. |

Press **Ctrl+C** to stop. Connected users are told the server is shutting down.

## 4. Check it from another computer

```bat
opentalk-ping my-server.example.org --fingerprint SHA256:AB:CD:...
```

A successful check prints the server name and the round-trip time. If you leave out `--fingerprint`, the tool refuses the connection and shows the fingerprint the server presented.

## 5. Reaching the server from the internet

- Allow **TCP port 10350** (or your chosen port) through the firewall on the server machine. Windows Defender Firewall asks the first time the server starts.
- On a home network, forward that TCP port on your router to the server computer.
- Some internet providers use carrier-grade NAT, which makes incoming connections impossible without a VPS or a VPN. Ask your provider, or check whether your router's WAN address matches what websites report as your public IP.
- Voice will also need a **UDP** port (the same number by default) when audio arrives in Phase 4.

## Security notes

- Connections use TLS 1.3 only. Older clients and unencrypted connections are refused.
- Each IP address may open at most 10 connections. Silent connections are dropped after 10 seconds, and clients that flood the server are disconnected.
- Logs at the default level contain no IP addresses and never contain message contents.
