# Data layout

**Status:** client and server layouts implemented and tested.

## Where the data root is

Chosen in this order (`AppPaths::resolve`):

1. **Custom:** the `--data-dir <directory>` command-line option.
2. **Portable:** a file named `OpenTalk.portable` next to `OpenTalk.exe`. All data goes to `<application folder>\data`. The portable zip from CI includes this file.
3. **Installed:** the per-user application data folder, `%LOCALAPPDATA%\OpenTalk\OpenTalk` on Windows.

No administrator rights are needed. The portable edition writes nothing to the registry. The installer (per user) puts the program in `%LOCALAPPDATA%\Programs\OpenTalk` and writes only its uninstall entry and install folder under `HKEY_CURRENT_USER`; uninstalling keeps the data root.

**Remembered passwords** (only if the user ticks "Remember my password") are kept in **Windows Credential Manager**, never in these files. Each entry is named `OpenTalk/<saved server ID>`, stored for this computer only. Control Panel, Credential Manager, Windows Credentials lists them.

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
├── server.lock          Held while a server uses the folder (a second server, --restore and
│                        --recover-owner refuse to run while it is held)
├── backups/             Automatic backups opentalk-YYYYMMDD-HHMMSS.db (UTC; newest 7 kept by
│                        default), and before-restore-<time>.db copies made by --restore
└── tls/
    ├── server.crt       Certificate (created on first start if missing)
    └── server.key       Private key (owner-only on Linux and macOS)
```

## Planned additions

| Path | Contents | Phase |
|---|---|---|
| `soundpacks/<name>/` | User soundpacks with a `soundpack.json` manifest. | 3/4 |
| `cache/` | Disposable data; safe to delete. | as needed |

A headless server uses the same `servers/<name>/` layout under a directory given with `--data-dir`.
