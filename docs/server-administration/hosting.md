# Hosting a server for others

This guide takes a server from "works on my computer" to "people on the internet can use it reliably". Start with the [quick start](quick-start.md) if you have not run `opentalk-server` yet.

**Status:** the server, backups, the container image and the systemd unit are implemented and tested in CI on Linux and Windows. Certificate renewal with Let's Encrypt and running on a real VPS are documented, but **have not yet been tried by a person**.

## Choose where it runs

| Where | Good for | Watch out for |
|---|---|---|
| **Your own PC** (Server menu → Host a server, or `opentalk-server`) | Friends and small groups | It is only up while your PC is on. Your router must forward the port, and some providers' carrier-grade NAT makes that impossible. |
| **A small VPS** (1 CPU, 1 GB RAM is plenty) | A community that wants the server always on | A few euros or dollars a month. Linux with systemd or Docker is easiest. |
| **A home server or Raspberry Pi-class machine** | Always on, at home | Build from source (see [building](../development/building.md)); the same port forwarding as for a PC. |

**How much it can carry:** forwarding one voice packet to 50 listeners takes about 51 µs of one CPU core ([benchmarks](../audio/benchmarks.md)). The connection limit (`--max-users`, default 200) will matter long before the CPU does. Each person talking uses about 32–64 kbit/s upload from the server **per listener**.

## Open the port

OpenTalk uses **one port number for both TCP and UDP**: 10350 by default.

- **TCP** carries logins, text and control messages, and is required.
- **UDP** carries voice faster. Without it, voice falls back to TCP with a little more delay.

| System | Command |
|---|---|
| Windows (PowerShell as administrator) | `New-NetFirewallRule -DisplayName "OpenTalk" -Direction Inbound -Protocol TCP -LocalPort 10350 -Action Allow` and the same with `-Protocol UDP` |
| Ubuntu or Debian with ufw | `sudo ufw allow 10350/tcp && sudo ufw allow 10350/udp` |
| Fedora or RHEL with firewalld | `sudo firewall-cmd --permanent --add-port=10350/tcp --add-port=10350/udp && sudo firewall-cmd --reload` |
| Home router | Forward TCP **and** UDP 10350 to the server computer's local address. |

A VPS provider may also have its own firewall ("security groups") in its control panel; open the port there too.

## Run it as a service

### Linux with systemd

[`packaging/linux/opentalk-server.service`](../../packaging/linux/opentalk-server.service) runs the server as its own unprivileged user, keeps its data in `/var/lib/opentalk`, restarts it if it crashes, and restricts what it can touch on the system.

```sh
sudo useradd --system --home-dir /var/lib/opentalk --shell /usr/sbin/nologin opentalk
sudo install -m 755 opentalk-server opentalk-ping /usr/local/bin/
sudo cp opentalk-server.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now opentalk-server
journalctl -u opentalk-server -n 30          # the start-up output, including the fingerprint
```

Create the owner's account (it asks for the password):

```sh
sudo -u opentalk opentalk-server --data-dir /var/lib/opentalk --create-account yourname --role owner
```

Add options such as `--allow-guests` or `--name "Book club"` to `ExecStart`. Then run `sudo systemctl daemon-reload && sudo systemctl restart opentalk-server`.

### Docker

The repository's [`Dockerfile`](../../Dockerfile) builds a small image with `opentalk-server` and `opentalk-ping`. It runs as a non-root user and keeps everything in a `/data` volume.

```sh
docker build -t opentalk-server .
docker run -d --name opentalk --restart unless-stopped \
    -p 10350:10350/tcp -p 10350:10350/udp -v opentalk-data:/data \
    opentalk-server --name "Book club"
docker logs opentalk                                   # the fingerprint
docker exec -it opentalk opentalk-server --data-dir /data --create-account yourname --role owner
```

`docker stop` shuts the server down gracefully: connected people are told.

### Windows

Run `opentalk-server.exe` in a console, or let the OpenTalk app host it (Server menu). To start it automatically when the computer starts, use Task Scheduler:

1. Create a task that runs "whether the user is logged on or not".
2. Set the trigger to "At startup".
3. Set the action to start `opentalk-server.exe --data-dir C:\OpenTalkServer`.

## Certificates

**The default: a self-signed certificate.** The server creates it on first start. Users check its **fingerprint** the first time they connect, and OpenTalk remembers it (trust on first use). Publish the fingerprint where your users can see it is genuinely from you, for example on your community's website. If the certificate ever changes, users see a clear warning.

**A certificate from a public authority** (for example Let's Encrypt) lets users connect without checking a fingerprint. It needs a domain name pointing at the server.

```sh
sudo certbot certonly --standalone -d chat.example.org      # needs port 80 free for a moment
sudo opentalk-server ... --tls-cert /etc/letsencrypt/live/chat.example.org/fullchain.pem \
                         --tls-key /etc/letsencrypt/live/chat.example.org/privkey.pem
```

- **Access to the key:** the `opentalk` user must be able to read the key. One way is a deploy hook that copies the two files into `/var/lib/opentalk/tls/` with the right owner.
- **Renewal:** the server reads the certificate only when it starts, so restart it after each renewal. For example, add `--deploy-hook "systemctl restart opentalk-server"` to certbot.

## Check it from outside

Ask someone on another network to run `opentalk-ping` (it ships with OpenTalk), or use your phone's hotspot:

```sh
opentalk-ping chat.example.org --fingerprint SHA256:AB:CD:... --voice
```

| Result | Meaning |
|---|---|
| `Round trip: … ms` and `Voice: UDP works`, exit code 0 | Everything gets through. |
| `Voice: UDP does not get through…`, exit code 3 | TCP works but UDP is blocked: open or forward **UDP** 10350. Voice still works, with more delay. |
| `Failed: …`, exit code 1 | The server cannot be reached: check the address, the TCP port, the firewall and the router. |

Testing from the server's own network can succeed even when outsiders cannot connect, because many routers do not "hairpin" connections to their own public address.

## Backups

The server backs up its database automatically:

- **When:** once a day, starting when it first starts.
- **Where:** `<data>/backups/opentalk-YYYYMMDD-HHMMSS.db`, with the time in UTC.
- **How many:** the newest 7 are kept.

A backup holds the accounts, channels, roles and rules, sanctions, reports, appeals, the audit log and the settings. The certificate is in `<data>/tls`; copy that folder once and keep it safe.

| Command | Does |
|---|---|
| `--backup-every-hours <n>` | How often (default 24; `0` turns automatic backups off). |
| `--keep-backups <n>` | How many automatic backups to keep (default 7). |
| `--backup-dir <folder>` | Put them somewhere else, for example another disk. |
| `--list-backups` | Lists the automatic backups. |
| `--backup <file>` | Makes a backup now. Safe while the server is running. |
| `--restore <file>` | Replaces the database with a backup. **Stop the server first**; it refuses while a server uses the folder. The current database is copied to `backups/before-restore-<time>.db` first, so a restore can be undone. |

Backups are checked before a restore: a damaged file, or one from a newer OpenTalk, is refused. A backup from an older OpenTalk is upgraded when restored.

**Keep a copy on another machine.** Backups on the same disk do not survive that disk failing. For example, copy `backups/` with `rsync` or your provider's snapshot feature.

## Upgrading

1. Run `opentalk-server --data-dir … --backup before-upgrade.db`.
2. Stop the server.
3. Replace the programs: the zip's contents, the installer, or a rebuilt container image.
4. Start it again.

A newer version upgrades the database by itself and keeps a copy named `opentalk.db.v<N>-backup` first. An older version refuses a database from a newer one rather than damage it.

## Keeping an eye on it

- **Logs:** standard output and standard error (`journalctl -u opentalk-server`, `docker logs opentalk`, or the console window). At the default level they contain no IP addresses, passwords or message text.
- **Statistics:** Moderation → Administration → Statistics in the app shows users online, uptime and traffic.
- **Audit log:** `--verify-audit` checks it is intact. `--export-audit <file>` writes it out; keep exported copies elsewhere, because they show any later tampering.

## Security checklist

- [ ] The owner account has a strong password, and only trusted people are administrators.
- [ ] Guests are allowed only if you want them (`--allow-guests`, or Server settings in the app).
- [ ] Only TCP and UDP 10350 are open to the internet.
- [ ] The server runs as its own unprivileged user (systemd unit or container), not as root or an administrator.
- [ ] Backups are copied off the machine regularly, and a restore has been tried once.
- [ ] The fingerprint is published where users can see it comes from you, or the server uses a public certificate.
- [ ] You keep the server up to date: watch the repository's releases and security advisories.
