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
| 21 | Arrow through the channel tree. | Channels read as "Name, 2 users, password protected, temporary" where these apply. People read as "name (you), administrator" or "name, guest". |
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

## Scaling and contrast (A-11)

| # | Do | Expect |
|---|---|---|
| 31 | Set Windows scaling to 200% and restart OpenTalk. | All text is visible, nothing overlaps, and the window is usable. |
| 32 | Turn on a Windows contrast theme. | The application follows the theme and all text is readable. |
