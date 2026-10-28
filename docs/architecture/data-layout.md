# Data layout

**Status:** client and server layouts implemented and tested.

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
│   └── server-profiles.json                   Saved servers (no passwords)
├── logs/
│   └── opentalk-client.log
├── logs/plugins/<id>.log                    Each plugin's log (standard error and "log" messages)
├── plugins/<id>/                              Installed plugins (copied from the folder the user chose)
├── plugin-data/<id>/                          Each plugin's own writable folder
├── config/plugins.json                        Which plugins are on, what they may do, and the manifest
│                                              hash the user agreed to
└── servers/
    └── local/                                 The server you host from the app (same layout as below)
```

## Server data folder (implemented)

`opentalk-server --data-dir <folder>` (default: the per-user application data folder of `opentalk-server`):

```
<folder>/
├── opentalk.db          SQLite (schema 3): accounts (role, Argon2id hash, bot), persistent channels, roles,
│                        permission rules, sanctions, reports, appeals, audit log, settings
├── opentalk.db-wal/-shm SQLite working files (write-ahead log)
├── opentalk.db.v<N>-backup  Copy made before a database upgrade
└── tls/
    ├── server.crt       Certificate (created on first start if missing)
    └── server.key       Private key (owner-only on Linux and macOS)
```

## Planned additions

| Path | Contents | Phase |
|---|---|---|
| Windows Credential Manager entries | Optional remembered passwords (today the app asks each time). | later |
| `soundpacks/<name>/` | User soundpacks with a `soundpack.json` manifest. | 3/4 |
| Server `backups/` | Database backups. | 8 |
| `cache/` | Disposable data; safe to delete. | as needed |

A headless server uses the same `servers/<name>/` layout under a directory given with `--data-dir`.
