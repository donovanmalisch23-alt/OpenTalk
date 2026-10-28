# Moderating and administering a server

**Status:** implemented and tested in Phase 5 (see the [status report](../status.md)). Not yet checked with NVDA by a person.

This guide is for server owners, administrators and moderators. The rules behind it are in the [moderation model](../security/moderation-model.md).

## Roles

Every account has one role. Guests (people without an account) always have the Guest role.

| Role | Rank | Can do |
|---|---|---|
| Owner | 100 | Everything, including transferring ownership. A server has exactly one owner. |
| Administrator | 90 | Everything except transferring ownership: accounts, roles, permissions, settings, all moderation. |
| Moderator | 50 | Warn, mute, kick, ban for up to the longest temporary ban, move people, lift sanctions, review reports, decide appeals, read the audit log, see statistics. |
| Registered user | 10 | Join channels, talk, send messages, create temporary channels. |
| Guest | 5 | Join channels, talk, send messages. |
| Restricted | 1 | Join channels only. Used by the "restriction" sanction. |

**Ranks protect people from their peers and superiors:** you can only sanction, move, or change the role of people whose role ranks **below** yours, and you can only give roles that rank below yours. So a moderator cannot ban another moderator, and an administrator cannot touch the owner.

Owners and administrators can create **custom roles** (rank 2 to 89) with any of the capabilities they hold themselves, and change the capabilities of the built-in roles. In the app: **Moderation → Administration → Roles**.

## The first owner

- **Hosting from the app:** the account you choose in "Host a server" becomes the owner. If the server already has an owner (you hosted before under another name), the new account becomes an administrator.
- **A standalone server:**

  ```bat
  opentalk-server --data-dir C:\OpenTalkServer --create-account yourname --role owner
  ```

  `--role` also accepts `admin`, `moderator`, `user` and `restricted`.
- **Upgrading from Phase 3 or 4:** the database is upgraded on first start (a copy of the old file is kept as `opentalk.db.v1-backup`). The earliest administrator becomes the owner, and this is written to the audit log.

## Locked out? Recover the owner

If the owner's password is lost or the account was damaged, someone with access to the server's files can make an existing account the owner. **Stop the server first**, then:

```bat
opentalk-server --data-dir C:\OpenTalkServer --recover-owner yourname
```

The previous owner, if any, becomes an administrator. The recovery is recorded in the audit log, so it is visible to everyone who can read the log. Use `--set-password yourname` first if the password is the problem.

## Moderating from the app

Select a person in the channel tree or the user list, then use the **Moderation** menu (Alt+O) or the context menu (Shift+F10 or the Menu key). Only the actions you may use are shown.

| Action | What happens |
|---|---|
| **Warn** | The person is told the warning and the reason. Nothing else changes. |
| **Mute** | Voice mute, text mute, or both, for a time or until lifted. Everyone sees "muted" next to the name. |
| **Kick** | Disconnects the person, who can come back at once. A kick "only in this channel" moves them out of that channel. |
| **Ban** | Disconnects the person and stops them logging in (or joining that channel) until it ends. "Also block their IP address" also stops new accounts and guests from the same connection, which can affect other people who share it. |
| **Restrict** | The person can join channels but cannot talk or send messages, server-wide. |
| **Move** | Moves the person to another channel. |

**Reasons** are required for bans, restrictions, and mutes longer than one hour, and the person is always told the reason. Bans longer than the server's "longest temporary ban" (30 days by default), or until lifted, need the permanent-ban permission.

The person sanctioned sees a line in their history such as "Mute until 2026-11-02 14:30, from mod. Reason: shouting." A kicked or banned person sees why they were disconnected, and for bans the contact you set in the server settings. The app does not reconnect them automatically.

## The Administration dialog

**Moderation → Administration** (Ctrl+Shift+A). Each area is a tab; Ctrl+Tab and Ctrl+Shift+Tab move between them, and F5 reloads the current one. You only see the tabs you may use. Lists are read by screen readers row by row, with each column named.

| Tab | Use it to |
|---|---|
| Sanctions | See active sanctions (or all, with "Show ended and lifted sanctions"), lift one with a reason, or sanction an account that is not online. |
| Reports | Read reports from users. Mark one as handled or dismiss it, with a note, or sanction the reported account. Reporters' names are shown only here; the reported person is never told who reported them. |
| Appeals | Decide appeals: uphold, reduce (choose the new length, counted from when the sanction was given) or overturn, with a reason that the person is told. **The moderator who gave a sanction cannot decide its appeal** while someone else can. |
| Accounts | Create accounts, change roles, reset passwords, delete accounts, and (owner only) transfer ownership. |
| Roles | Create, change and delete roles, choosing their capabilities from a list of check boxes (Space checks or unchecks). |
| Channel permissions | Add or delete rules (below). |
| Audit log | Read every moderation and administration action, newest first, 100 at a time, and see whether the log is intact. |
| Server settings | Name, welcome message, guests, most people online, highest voice quality, longest temporary ban, and a contact for banned people. Changes apply to new connections at once and are kept across restarts. |
| Statistics | Uptime, people online, connections, channels, voice packets, sanctions, open reports and appeals, and audit log state. |

Settings saved here **take precedence over** the `--name`, `--allow-guests` and `--max-users` options the server was started with.

## Channel permissions and channel moderators

A **rule** says that, in one channel (and its sub-channels) or on the whole server, a role or a single account is **allowed** or **denied** one capability. The most specific rule wins: a channel before its parents, an account before its role, and deny before allow when both apply at the same level. Without a rule, the role's own capabilities apply.

Examples:

| Goal | Rules |
|---|---|
| A quiet room where registered users cannot talk | Quiet room: role Registered user, `voice.transmit`, deny |
| ...except alice, who leads it | Quiet room: account alice, `voice.transmit`, allow |
| Make bob moderator of the Games channel only | Games: account bob, allow `user.warn`, `user.mute`, `user.kick`, `user.move`, `sanction.revoke` (one rule each) |
| Guests may not send private messages | Whole server: role Guest, `text.private`, deny |

A channel moderator made with **account** rules may act on people of the same rank inside that channel (bob, a registered user, may kick other registered users from Games), but never on higher ranks. Rules for a whole role do not give this exception: members of a role can only act on lower ranks. Their app shows the moderation actions because the server tells it which capabilities they hold in some channel.

## The audit log

Every sanction (given, ended or lifted), move, report, appeal, role, rule, account, channel and settings change, ownership transfer and owner recovery is recorded with the time, who did it, the target, the channel, the reason and details. So is each server start. Each entry contains a SHA-256 hash of the entry before it, so changing or deleting an entry breaks the chain.

| Command | What it does |
|---|---|
| `--verify-audit` | Checks the chain and exits: 0 if intact, 2 if it was changed. |
| `--export-audit <file>` | Writes the whole log as text, oldest first, with the chain state at the end. Keep copies somewhere else: comparing them later shows tampering. |
| `--audit-retention-days <days>` | How long entries are kept (default 365; 0 keeps them forever). Older entries are removed at start-up and once a day. |

The server checks the chain at start-up and logs a warning if it is broken. The Administration dialog and the statistics show the same state.

**What the audit log cannot do:** someone with access to the server's files can still change the database. The chain shows *that* something changed, from which entry, but cannot undo it or stop it. Exported copies kept elsewhere are the defence.

## Reports and appeals from the user's side

- Anyone can report someone: select them, **Moderation → Report selected user**, and say what happened. Holders of the report-review permission who are online get a server message about it.
- People with an account can appeal each sanction once: **Moderation → My sanctions and appeals**, select it, **Appeal**. They are told the decision and reason. Guests cannot appeal; the ban message gives them the server's contact instead.
