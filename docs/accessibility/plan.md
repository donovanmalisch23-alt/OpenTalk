# Accessibility plan

Accessibility blocks releases. A regression in any criterion below is a release-blocking bug.

**Primary target:** NVDA (current stable release) on Windows 10 and Windows 11 with the OpenTalk Windows build (Qt 6.8 LTS or later). **Secondary target:** Windows Narrator. **Later:** Orca on Linux, VoiceOver on macOS.

Qt exposes widgets to screen readers through **UI Automation** on Windows. Standard widgets are used wherever possible. A custom accessibility interface is written only when NVDA testing shows that a standard one is not enough.

## Acceptance criteria

"Auto" means checked by `tests/unit/tst_mainwindow.cpp` on every CI run. "NVDA" means checked with the [manual test script](nvda-test-script.md) on every release candidate.

| ID | Criterion | Check | Status |
|---|---|---|---|
| A-1 | Every focusable, visible control has a non-empty accessible name. | Auto | Passing for the main window and every settings page |
| A-2 | Every feature can be used with the keyboard alone; no mouse-only actions (hover, drag-and-drop or right-click without a keyboard equivalent). | Review, NVDA | Phase 1 features: yes |
| A-3 | **F6** and **Shift+F6** cycle through the main panes in visual order; **Ctrl+1–4** jump to channels, users, history and message input. | Auto | Passing |
| A-4 | NVDA announces name, role, and state or value when focus lands on every control in the main window and dialogs. | NVDA | **Not yet run.** Needs a Windows machine with NVDA |
| A-5 | No keyboard traps: Tab and Shift+Tab move through every control in each window, and Escape closes dialogs. | Auto (partly), NVDA | Partly automated |
| A-6 | Focus returns to the control that opened a dialog after the dialog closes. | NVDA | Not yet run |
| A-7 | Live events (status changes, incoming messages, people joining and leaving) are announced without moving focus, using polite announcements. Each kind can be turned off. | Auto (via test hook), NVDA | Implemented on Qt 6.8 and later; automated tests check what is announced; **not yet heard with NVDA** |
| A-8 | Announcement flooding is controlled: joins and leaves within 1.5 seconds are combined (for example "3 users joined"), announcements are at least 0.5 seconds apart, and at most 5 wait in line (the oldest are dropped). | Auto | Implemented and tested (`tst_announcements`) |
| A-9 | Sound is never the only signal. Every sound event has a text or state equivalent (history line, status bar or announcement). | Review | Followed: every event already has a history line; sounds arrive in Phase 4 |
| A-10 | Settings dialog: the category list and the page are reachable with Tab; **Ctrl+Tab**, **Ctrl+Shift+Tab**, **Ctrl+PgDown** and **Ctrl+PgUp** switch category; every field has a label and a description. | Auto, NVDA | Automated part passing |
| A-11 | Usable at 200% Windows scaling and with Windows high-contrast themes: no clipped text and no hard-coded colours. | Manual | Not yet run; no custom colours are used |
| A-12 | Errors are reported in text that NVDA reads (message boxes or announced status), say what happened and what to do, and never rely on colour alone. | Review, NVDA | Followed in current code |

## Keyboard map (implemented)

| Keys | Action |
|---|---|
| F6 / Shift+F6 | Next / previous pane |
| Ctrl+1 | Channels |
| Ctrl+2 | Users |
| Ctrl+3 | Conversation history |
| Ctrl+4 | Message input |
| Alt+C, Alt+S, Alt+M, Alt+U | Jump to the Channels, History, Message and Users panes using their labels |
| Ctrl+O | Connect to a server |
| Ctrl+D | Disconnect |
| Enter (channel tree) | Join the selected channel, or send a private message to the selected user |
| Enter (users list) | Send a private message to the selected user |
| Shift+F10 or Menu key (channel tree, users list) | Actions for the selected channel or person: join, create, edit, delete, private message, volume, and the moderation actions the user may use |
| Ctrl+N | Create a channel |
| Ctrl+P | Private message to the selected user |
| Enter (message box) | Send to your channel |
| F4 | Settings |
| Push-to-talk key (chosen in Settings → Audio; global on Windows) | Talk while held |
| Ctrl+T | Talk on or off |
| Ctrl+M | Mute or unmute the microphone |
| Ctrl+Shift+M | Mute or unmute everything you hear |
| Ctrl+Shift+V | Volume for the selected person |
| Ctrl+Shift+F | Stream a file into the channel |
| Ctrl+Shift+K | Pause or resume the file being streamed |
| Ctrl+Shift+J / Ctrl+Shift+L | Back / forward 10 seconds in that file |
| Ctrl+Shift+X | Stop streaming |
| Ctrl+Shift+I | Say what is streaming, and where |
| Ctrl+Shift+C | Share computer audio on or off (Windows) |
| Ctrl+Shift+A | Administration (moderators and administrators) |
| Ctrl+Tab / Ctrl+Shift+Tab, F5 (Administration) | Next / previous tab; reload the tab |
| Ctrl+Q | Exit |
| Alt+F, Alt+R, Alt+N, Alt+A, Alt+O, Alt+P, Alt+V, Alt+H | File, Server, Channel, Audio, Moderation, Plugins, View and Help menus |

Menu mnemonics deliberately avoid the pane labels' Alt+C, Alt+S, Alt+M and Alt+U. New shortcuts are added to this table and checked for conflicts.

## Design rules for contributors

1. Prefer standard Qt widgets. If you subclass one, check it with NVDA.
2. Give every input a visible `QLabel` with `setBuddy()`, or a self-describing text (checkboxes and buttons). Also set `setAccessibleName` where the visible text is not enough.
3. Put help text in `setAccessibleDescription` as well as on screen.
4. Never change focus without user action, except when opening a dialog or after an action whose purpose is to move focus.
5. Use `QAccessibleAnnouncementEvent` for live updates; never steal focus to report something.
6. Never disable a control silently. Explain why something is unavailable, or hide features that do not exist yet.
7. Add new focusable controls to the automated test if they are not already covered by the window-wide name check.
