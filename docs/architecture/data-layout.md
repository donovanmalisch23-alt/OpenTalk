# Data layout

**Status:** client layout implemented and unit-tested; server layout designed.

## Where the data root is

Chosen in this order (`AppPaths::resolve`):

1. **Custom:** the `--data-dir <directory>` command-line option.
2. **Portable:** a file named `OpenTalk.portable` next to `OpenTalk.exe`. All data goes to `<application folder>\data`. The portable zip from CI includes this file.
3. **Installed:** the per-user application data folder, `%LOCALAPPDATA%\OpenTalk\OpenTalk` on Windows.

Nothing is written to the registry, and no administrator rights are needed.

## Client files (implemented)

```
<data root>/
├── config/
│   ├── client-settings.json                   Settings (see reference/client-settings.md)
│   ├── client-settings.json.corrupt-<time>    Damaged file moved aside, if that ever happens
│   └── client-settings.json.v<N>-backup-<time> Copy made before a format upgrade
└── logs/
    └── opentalk-client.log
```

## Planned additions

| Path | Contents | Phase |
|---|---|---|
| `config/server-profiles.json` | Saved servers. Passwords are **not** stored here; they go to Windows Credential Manager. | 3 |
| `config/known-servers.json` | Pinned server certificate fingerprints (trust on first use). | 3 |
| `soundpacks/<name>/` | User soundpacks with a `soundpack.json` manifest. | 3/4 |
| `plugins/<id>/` and `config/plugins/<id>.json` | Installed plugins and their settings. | 6 |
| `servers/<name>/server.json` | Configuration of a locally hosted server. | 3 |
| `servers/<name>/opentalk.db` | Server database (SQLite): accounts, channels, permissions, sanctions, audit log. | 3/5 |
| `servers/<name>/tls/` | Server certificate and key, readable by the current user only. | 3/8 |
| `servers/<name>/backups/` | Database backups. | 8 |
| `cache/` | Disposable data; safe to delete. | as needed |

A headless server uses the same `servers/<name>/` layout under a directory given with `--data-dir`.
