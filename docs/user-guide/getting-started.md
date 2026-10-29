# Getting started

OpenTalk runs on Windows 10 (version 1809 or later) and Windows 11. It works fully with the keyboard and a screen reader; it is tested with NVDA.

## Installer or portable?

Both are on the [Releases page](https://github.com/donovanmalisch23-alt/opentalk/releases) and contain the same programs.

| | Installer (`OpenTalk-<version>-setup-x64.exe`) | Portable (`OpenTalk-<version>-portable-windows-x64.zip`) |
|---|---|---|
| Needs administrator rights | No (installs for you only) | No |
| Program location | `%LOCALAPPDATA%\Programs\OpenTalk` | Wherever you unzip it, for example a USB stick |
| Start menu entry, uninstaller | Yes | No |
| Settings and saved servers | Your application data folder | The `data` folder next to `OpenTalk.exe` |

**Installer:** run it and follow the pages: Welcome, licence, folder, then Finish. Each page is a standard Windows dialog that NVDA reads. Uninstall from Windows Settings, Apps; your settings are kept, in case you install again.

**Portable:** unzip the folder and run `OpenTalk.exe`. Delete the folder to remove it.

**"Windows protected your PC":** OpenTalk is not yet signed with a code-signing certificate, so Windows SmartScreen may warn about a new program.

1. Check that the file came from the project's Releases page.
2. If you want to, compare its SHA-256 checksum with `SHA256SUMS.txt` on the same page: in PowerShell, `Get-FileHash <file>`.
3. Choose "More info", then "Run anyway".

## Your first connection

1. Press **Ctrl+O** (Server, Connect).
2. Type the server's **address** and, if it is not 10350, its **port**.
3. Choose **Log in as**: Guest (just a nickname) or Account (username and password, created by the server's administrator).
4. Press **Enter**.

The first time, OpenTalk shows the server's **certificate fingerprint** and asks whether you trust it.

- Compare it with the fingerprint the server's owner published. If it matches, choose "Trust and connect". OpenTalk remembers it.
- If a server's fingerprint ever changes, you are warned. Ask the owner before trusting the new one.

**Hosting your own:** Server menu, "Host a server on this computer". You become the server's owner. See the [server quick start](../server-administration/quick-start.md) and [hosting guide](../server-administration/hosting.md).

## Remembering your password

In the connect dialog, tick **Remember my password**.

- **Where it goes:** the password is stored in **Windows Credential Manager**, protected by your Windows sign-in and only on this computer. It is never written to OpenTalk's files, not even in the portable edition.
- **When it is stored:** only after the server accepted it.
- **To forget it:** untick the box and connect, or delete the saved server. You can also delete the "OpenTalk/…" entries in Control Panel, Credential Manager, Windows Credentials.

## Moving around

| Keys | Go to |
|---|---|
| F6 / Shift+F6 | Next / previous area |
| Ctrl+1 to Ctrl+4 | Channels, users, history, message box |
| Enter on a channel | Join it |
| Ctrl+P | Private message to the selected person |
| F4 | Settings |

Every menu shows its shortcuts. The complete list is in the [keyboard map](../accessibility/plan.md#keyboard-map-implemented).

Next: [voice and sounds](voice-and-sounds.md), [plugins](plugins.md), [reports, sanctions and appeals](moderation.md), and [troubleshooting](troubleshooting.md).
