# Troubleshooting

## Connecting

| Problem | What to try |
|---|---|
| "Connection problem: … refused" or a time-out | Check the address and port with the server's owner. The server may be off, or its firewall or router may block TCP port 10350. The owner can check from outside with `opentalk-ping` ([hosting guide](../server-administration/hosting.md#check-it-from-outside)). |
| "The server's certificate changed" | Do not trust it until the owner confirms the new fingerprint. It is normal after the owner moved or reinstalled the server; it can also mean someone is intercepting the connection. |
| "Could not log in: that name is already in use" | Someone (perhaps you, in another window) is using that guest nickname. Choose another, or log in with your account. |
| "Could not log in" with an account | Check the username and password. After 10 wrong passwords from your address the server refuses logins for 10 minutes. |
| "Kicked" or "Banned" | The message gives the moderator's reason. A kick only disconnects you: you can connect again. While you can still log in (for example after a channel ban or a mute), Moderation, "My sanctions and appeals", lets you appeal. For a ban from the whole server, contact the server's owner another way. |
| It keeps reconnecting | OpenTalk reconnects by itself when the connection drops. The history says each time. Disconnect (Ctrl+D) to stop it. |

## Voice

| Problem | What to try |
|---|---|
| Nobody hears me | Check the status bar's voice field: "Microphone muted" means press Ctrl+M. In push-to-talk mode, set a key in Settings, Audio, or press Ctrl+T to talk. In voice activation mode, lower the level in Settings, Audio, while watching "Microphone level". Guests and restricted users may not be allowed to talk in some channels. |
| I hear nobody | Check that "Sound muted" is not shown (Ctrl+Shift+M toggles it), the person's volume (Ctrl+Shift+V), and the output device in Settings, Audio. |
| "Audio problem: …" in the history | A device is missing or in use. OpenTalk keeps trying and says when it works again. Choose another device in Settings, Audio. |
| Others hear themselves (echo) | Use headphones, or turn on "Remove echo when using speakers" in Settings, Audio. Echo cancellation cannot remove a screen reader's speech; for that, use headphones or push-to-talk. |
| Voice breaks up | Lower "Voice quality" in Settings, Audio. If the history says voice uses the main connection, UDP is blocked between you and the server. That works, with more delay; the server's owner can open UDP. |
| Fans or hum are sent | "Reduce background noise" is on by default in Settings, Audio. "Remove rumble" in Settings, Voice Effects and Media, also helps. |
| My file or computer audio is not heard | The channel or your role may not allow streaming. Guests cannot stream by default. |
| Sharing computer audio sends other people's voices back (Windows 10) | Windows 10 cannot leave OpenTalk out of shared computer audio. Send OpenTalk's sound to another device in Settings, Audio (for example headphones), or use Windows 11. |

## Screen readers

- **Announcements are not spoken:** check Settings, Accessibility. Spoken status announcements need Qt 6.8 or later, which the released builds use. Everything announced is also written to the history (Ctrl+3).
- **Too much is announced:** turn off categories in Settings, Accessibility.
- **A control is read without a name:** please report it as a bug. Every control should have a name, and the automated tests check that.

## Plugins

If OpenTalk misbehaves after turning on a plugin, start it with plugins off:

```bat
OpenTalk.exe --no-plugins
```

Plugins, "Manage plugins", then "Show log" shows what a plugin reported. A plugin that keeps crashing is turned off automatically.

## Logs and reporting a problem

- **App log:** `logs\opentalk-client.log` in the data folder. Help, About OpenTalk, shows where the data folder is. Settings, "Logging and Diagnostics", can switch to more detail.
- **Server log:** the server's console output, or `journalctl -u opentalk-server` or `docker logs`.
- **What logs contain:** at the default level, no passwords, message text or IP addresses. Read a log before you share it anyway.
- **Support file:** Help, "Create a support file" collects what helps to find a problem: OpenTalk's version, Windows, the sound devices, plugins, your settings and the last 200 lines of the log. It leaves out passwords, messages, server addresses, other people's names, your Windows user folder (shown as `%USERPROFILE%`) and folders you used. The dialog shows the whole file first; read it, then save it or copy it.
- **Where to report:** open an issue on the project's GitHub page, describe what you did and what happened, and attach the support file. Report security problems privately, as described in `SECURITY.md`.
- **"OpenTalk did not close properly last time":** OpenTalk crashed or was ended (for example by Task Manager or a power cut). If it crashed, please report it with a support file; the end of the log shows what it was doing.

## "OpenTalk is already running"

Another copy is using the same data folder: switch to it with Alt+Tab. If you are sure no copy is running (for example, the portable copy is on a USB drive that was last used on another computer, which then crashed), delete `opentalk.lock` in the data folder.

## Starting fresh

- **Settings:** close OpenTalk and delete `config\client-settings.json` in the data folder; defaults are used next time.
- **Saved servers:** delete `config\server-profiles.json`.
- **Remembered passwords:** remove the "OpenTalk/…" entries in Credential Manager.
