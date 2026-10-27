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

## Scaling and contrast (A-11)

| # | Do | Expect |
|---|---|---|
| 17 | Set Windows scaling to 200% and restart OpenTalk. | All text is visible, nothing overlaps, and the window is usable. |
| 18 | Turn on a Windows contrast theme. | The application follows the theme and all text is readable. |
