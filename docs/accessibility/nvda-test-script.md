# NVDA manual test script

Run this on every release candidate with NVDA (latest stable release, default settings) on Windows 10 and on Windows 11. Record the date, the NVDA and OpenTalk versions, and pass or fail with notes for each step in the release issue.

## Setup

1. Unzip the portable build (CI artifact **OpenTalk-portable-windows-x64**) to a folder, for example `C:\OpenTalk`.
2. Start NVDA, then run `OpenTalk.exe`.

## Main window (A-3, A-4)

| # | Do | Expect |
|---|---|---|
| 1 | Start the app. | NVDA says "OpenTalk", then focus lands on the channel tree: "Channels, tree view, Not connected to a server". |
| 2 | Press F6 repeatedly. | Focus goes to: Conversation history (edit, read-only), Message (edit), Users (list), Channels, in that order. Each is announced with its name. |
| 3 | Press Shift+F6. | Same order, reversed. |
| 4 | Press Ctrl+1, Ctrl+2, Ctrl+3 and Ctrl+4. | Focus goes to Channels, Users, History and Message respectively. |
| 5 | In History, use the arrow keys and Shift+arrows. | The welcome text is read line by line and can be selected. |
| 6 | In Message, type "hi" and press Enter. | NVDA announces "Message not sent: you are not connected to a server." (Qt 6.8 or later). The text "hi" stays in the field. |
| 7 | Press NVDA+End (read status bar). | "Message not sent: …" is read. |
| 8 | Press Alt, then use the arrow keys through the File, View and Help menus. | Every item is read with its shortcut. |
| 9 | Help → About OpenTalk. | The dialog text is read, including the version and licence. Closing it returns focus to where it was (A-6). |

## Settings (A-10, A-5, A-6)

| # | Do | Expect |
|---|---|---|
| 10 | Press F4. | "Settings, dialog", then "Settings categories, list, General". |
| 11 | Press the Down arrow. | The category changes to Accessibility, then Logging and Diagnostics. |
| 12 | Press Tab from the list. | Focus moves into the page. Each control's name, role, state and description is read. |
| 13 | Press Ctrl+Tab and Ctrl+Shift+Tab from inside a page. | The category changes, and the new category is announced or can be found. **Note in the results whether NVDA announces the new page.** |
| 14 | Tab through the whole dialog several times. | No control is skipped and there is no trap. Apply is announced as unavailable when nothing has changed. |
| 15 | Change "Log detail", then press Enter on OK. | The dialog closes and focus returns to the control that was focused before F4. |
| 16 | Press F4, then Escape. | The dialog closes without saving. |

## Hosting and joining (A-4, A-6, A-7)

| # | Do | Expect |
|---|---|---|
| 19 | Server menu (Alt+R) → Host a server on this computer. | The dialog opens on "Server name". Every field reads its name. The help text for "Let other computers connect" and the administrator fields is read as the description. |
| 20 | Enter an administrator username and a password of 8+ characters, then press Enter. | Within a few seconds: "Connected to … as … You are in …" is announced. Focus is in the channel tree on the server's top channel. Windows may ask about the firewall. |
| 21 | Arrow through the channel tree. | Channels read as "Name, 2 users, password protected, temporary" where these apply. People read as "name (you), owner" (you host this server) or "name, guest". |
| 22 | Press Ctrl+N, type a channel name, and press Enter. | "You are now in …" is announced. The new channel and you inside it appear in the tree. |
| 23 | Ctrl+4, type a message, and press Enter. | The message box empties. The history (Ctrl+3) ends with "[time] you: message". |
| 24 | From a second computer or a second copy of OpenTalk (`OpenTalk.exe --data-dir C:\temp\second`), connect to this server as a guest. | On the first computer: "name joined" is announced once. On the second computer: a dialog shows the certificate fingerprint and asks whether to trust it. |
| 25 | On the second computer, select your user in the Users list and press Enter. Type a message and press Enter. | The first computer announces "Private from name: message" without moving focus. |
| 26 | Settings (F4) → Accessibility: untick "Announce people joining and leaving". Repeat step 24. | Nothing is announced; the history still shows "joined the server". |
| 27 | Server menu → Stop hosting. | "Stopping your server" is announced, then "Disconnected". The tree shows "Not connected to a server". |

## Connecting to someone else's server

| # | Do | Expect |
|---|---|---|
| 28 | Ctrl+O. | "Connect to a server, dialog", with focus in "Saved servers" (or "Address" if there are none). |
| 29 | Fill in the address, choose "Guest", enter a nickname, and press Enter. | The trust prompt reads the fingerprint in full. "Trust and connect" connects; Cancel leaves you disconnected with a history line saying why. |
| 30 | Disconnect, then connect again from Saved servers. | No trust prompt the second time. |

## Voice (Phase 4)

Use headphones, or two computers, so voices do not feed back into the microphone.

| # | Do | Expect |
|---|---|---|
| 33 | F4 → Audio (Ctrl+Tab to reach it). Tab through the page. | Every control reads its name and help: Microphone, Speakers or headphones, volumes "in percent", "How to talk", "Push-to-talk key, None (use Ctrl+T to talk)", Voice activation level, Voice quality, the UDP checkbox. The level meter is skipped (not focusable); "Microphone level: … dB" can be read with NVDA's review cursor. |
| 34 | Press "Change key…", then press and release Control+F12. | The dialog closes; the field reads "Ctrl+F12". Escape in that dialog cancels without changing anything. |
| 35 | OK. Connect to a server with a second person in the same channel. Hold Control+F12 and speak; release it. | A click at the start and end. The other person hears you. The status bar's voice field reads "Talking" while held. NVDA says nothing extra. |
| 36 | Switch to another program (Alt+Tab) and hold Control+F12. | It still transmits (Windows global push-to-talk). |
| 37 | The other person talks. Move to their name in the channel tree. | Their name reads normally (no repeated re-reading while they talk). NVDA+Tab or the description (NVDA+Shift+D in some configurations) includes "talking". |
| 38 | F4 → Accessibility → tick "Announce who starts talking". The other person talks. | "name talking" is spoken once per talk spurt. |
| 39 | Press Ctrl+M, then Ctrl+M again. | "Microphone muted", then "Microphone on" is spoken; the menu item Audio → Mute microphone is checked or not. |
| 40 | Press Ctrl+Shift+M twice. | "Sound muted", then "Sound on". While muted you hear nobody and no event sounds. |
| 41 | Press Ctrl+T, speak, press Ctrl+T. | "Talking on", then "Talking off". |
| 42 | Select the other person (channel tree or Users list) and press Ctrl+Shift+V. | "Volume for name" dialog; the spin box reads "Volume for name, in percent". Up arrow changes their volume at once. "Silence" and "Normal (100%)" work. |
| 43 | F4 → Sounds. Press "Play a test sound". Change the volume and try again. | A short sound plays at the new volume. |

## Scaling and contrast (A-11)

| # | Do | Expect |
|---|---|---|
| 44 | Set Windows scaling to 200% and restart OpenTalk. | All text is visible, nothing overlaps, and the window is usable. |
| 45 | Turn on a Windows contrast theme. | The application follows the theme and all text is readable. |

## Moderation (Phase 5)

Host a server (steps 19–20) so you are its owner. On a second computer or a second copy of OpenTalk, log in as a registered user (create one first: Moderation → Administration → Accounts → New account).

| # | Do | Expect |
|---|---|---|
| 46 | Alt+O. | The Moderation menu opens and reads Warn, Mute, Kick, Ban, Move selected user, Report selected user, My sanctions and appeals, Administration. On the second copy (a registered user) only Report, My sanctions and appeals are there. |
| 47 | Select the other person in the channel tree. Shift+F10. | The context menu includes the same moderation actions. |
| 48 | Choose Warn. | "Moderate name, dialog". Tab reads "What to do, Warning", "How long" (unavailable for warnings), "Where", "Reason" with its help ("Optional. The person is told the reason."), then Apply and Cancel. |
| 49 | Type a reason and press Enter. | "You warned name." is announced. The second copy announces "Warning, from (your name). Reason: … To appeal, use Moderation, My sanctions and appeals." |
| 50 | Choose Mute, set How long to 1 day, leave Reason empty, and press Enter. | A message says a reason is needed; focus returns to Reason. Type one and press Enter. The person reads as "name, muted" in the tree on both computers. |
| 51 | On the second copy, type a message and press Enter. | The status says the message was not sent because you are muted. |
| 52 | Ctrl+Shift+A on the first computer. | "Administration, dialog", focus on the tabs ("Sanctions, tab"). Ctrl+Tab moves through Reports, Appeals, Accounts, Roles, Channel permissions, Audit log, Server settings, Statistics. |
| 53 | On the Sanctions tab, Tab to the list and arrow through it. | Each row reads in full, for example "name, Sanction: Mute until …, Reason: …, By: you, Given: …, State: active". |
| 54 | Press "Lift sanction…", type a reason, and press Enter. | "The sanction on name was lifted." The second copy is told it was lifted, and "muted" disappears from the name. |
| 55 | On the second copy: Moderation → Report selected user (select the first person), type what happened, Enter. Then on the first computer: Reports tab, F5. | The report is listed with the reporter's name and reason. "Mark as handled…" and "Dismiss…" work and the result is announced. |
| 56 | On the second copy: Moderation → My sanctions and appeals. Select the warning, press "Appeal the selected sanction…", type a reason, OK. | "Your appeal was sent." On the first computer the Appeals tab shows it. Because you gave the warning and nobody else can decide, "Decide…" works for you; choose Overturn, a reason, Decide. The second copy is told "Your warning was overturned on appeal." |
| 57 | Kick the second copy (Moderation → Kick selected user, reason, Enter). | The second copy shows a "Kicked" message with the reason and does **not** reconnect by itself. |

## Plugins (Phase 6)

| # | Do | Expect |
|---|---|---|
| 58 | Alt+P. | The Plugins menu reads "Manage plugins…". |
| 59 | Enter. | "Plugins, dialog". The list reads "No plugins installed"; the details below explain what plugins are. |
| 60 | Install from folder…, choose `examples\plugins\dice` in the OpenTalk folder. | "Installed Dice roller. Turn it on to use it." The list item reads "Dice roller, version 1.0.0, off". |
| 61 | Turn on…. | "Turn on Dice roller?, dialog". The description and the warning about trusting plugins can be read. Focus is in "Permissions"; each item reads its text and "checked", and sensitive ones begin with "Sensitive:". Space unticks and ticks. Turn on. |
| 62 | Close the dialog. Alt+P. | The menu now also has "Roll a dice (Dice roller)". Choosing it puts "(your name) rolled …" in the history, read by NVDA as a channel message. |
| 63 | Alt+P, Manage plugins, select Dice roller, Show log. | "Log of Dice roller, dialog"; the log text can be read line by line. Escape closes it. |
| 64 | Turn off, then Remove…, Remove everything. | "Dice roller is off.", then "Removed Dice roller." The Plugins menu has only "Manage plugins…" again. |

## Voice processing, files and computer audio (Phase 7)

Two copies of OpenTalk, both logged in to your server as registered users (guests may not stream). Use headphones on at least one of them. Put a WAV, FLAC or MP3 file somewhere easy to find.

| # | Do | Expect |
|---|---|---|
| 65 | F4, Ctrl+Tab to Audio. Tab through the new switches. | "Reduce background noise, check box, checked", "Even out my voice level automatically, check box, not checked", "Remove echo when using speakers, check box, not checked", each followed by its help text. |
| 66 | Ctrl+Tab to "Voice Effects and Media". Tab through it. | Every control is read with its name: Remove rumble, Bass (in decibels), Treble, Voice pitch (in semitones), Streaming quality, Streaming volume, Hear files I stream, Mute files other people stream, Mute computer audio other people share. Set Voice pitch to 5, OK, and talk: the other copy hears a higher voice. Set it back to 0. |
| 67 | Alt+A. Arrow through the Audio menu. | It reads Talk, Mute microphone, Mute everything I hear, Stream a file into the channel (Ctrl+Shift+F), Pause streaming, Back 10 seconds, Forward 10 seconds, Stop streaming and What is streaming (those five unavailable), Share computer audio (Windows), Mute files and computer audio from others, Audio settings. |
| 68 | Ctrl+Shift+F, choose the file, Enter. | "Streaming *name*, *m:ss* long." The other copy hears it. With "Announce who starts talking" on, it says "*you* playing a file"; in its channel tree your entry is described as "playing a file", not "talking". |
| 69 | Ctrl+Shift+K, then Ctrl+Shift+I. | "Paused *name* at *m:ss*." then "Paused *name*, *m:ss* of *m:ss*." The status bar's voice field reads "Paused *name*". Ctrl+Shift+K again resumes. |
| 70 | Ctrl+Shift+L twice, then Ctrl+Shift+J. | Each press says the new position, for example "0:30 of 3:12". |
| 71 | On the other copy: Alt+A, "Mute files and computer audio from others". | "Files and computer audio from others muted." The file goes silent there, while your voice is still heard. Choose it again to undo. |
| 72 | Ctrl+Shift+X. | "Stopped streaming *name*." The other copy's tree no longer describes you as playing a file. |
| 73 | Windows 11: play something in another program, then Ctrl+Shift+C. | "Sharing computer audio." The other copy hears the program but not its own voice coming back when it talks. Ctrl+Shift+C again: "Stopped sharing computer audio." |
| 74 | Windows 10: Ctrl+Shift+C. | A warning is read: Windows cannot leave OpenTalk's own sound out, some voices may still be heard, and how to avoid it. Ask the other person whether they hear themselves; record the answer. |
