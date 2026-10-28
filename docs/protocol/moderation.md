# Moderation and administration messages

**Status:** implemented in Phase 5, unit-tested (`tst_messages`, `tst_moderation_server`, `tst_authorization`), fuzzed (`fuzz_message_decoder`, `fuzz_server_core`) and integration-tested with the desktop app (`tst_moderation_ui`).

These messages extend [protocol version 1](proposal.md). They use the same framing, CBOR maps with small integer keys, and request rules. The rules they enforce are described in the [moderation model](../security/moderation-model.md). Older clients that do not know these messages get `Error{UnknownMessage}` from the server, and the session continues.

## Changes to Phase 3 messages

| Message | Change |
|---|---|
| AuthResult (0x0008) | 3: role is now any role ID (see below). 4: capabilities held server-wide (array of capability numbers). 5: capabilities held only in some channels, for example by a channel moderator (array, optional). |
| StateSnapshot (0x0100) | 3: roles (array of roles, optional). |
| User fields | 3: role ID. 5: flags (optional; bit 0 voice muted, bit 1 text muted, bit 2 restricted). |
| ChannelUpdated (0x0103) | S→C. A channel (same fields as ChannelAdded) after someone edited it. |
| EditChannel (0x0113) | C→S. 1: request ID, 2: channel ID, 3: name, 4: topic, 5: maximum users, 6: change password (bool), 7: new password (omit to remove it). Needs `channel.edit` there. |
| UserUpdated (0x0203) | S→C. 1: user ID, 2: role ID, 3: flags. Sent to everyone when someone's role or mute changes. |
| Error | New codes: 17 Banned, 18 Kicked, 19 Muted. An Error without a request ID ends the session (see [proposal](proposal.md)). |

**Role IDs:** 0 Guest, 1 Registered user, 2 Administrator, 3 Owner, 4 Moderator, 5 Restricted. Owners create further roles with IDs from 100. A **role** is 1: ID, 2: name, 3: rank (0–100), 4: capabilities (array), 5: built in (bool).

**Capability numbers** are listed in [`Permissions.h`](../../src/moderation/include/opentalk/moderation/Permissions.h) and in the [moderation model](../security/moderation-model.md). They are never renumbered. A client must ignore numbers it does not know.

## Server to client

| Type | Name | Fields |
|---|---|---|
| 0x0500 | RolesChanged | 1: all roles. Sent to everyone when a role is created, changed or deleted. |
| 0x0501 | CapabilitiesChanged | 1: server-wide capabilities, 2: channel-only capabilities. Sent to one user when what they may do changes. |
| 0x0502 | SanctionNotice | A sanction (fields below) on this user: new, changed, ended, lifted or overturned. Sent at login for each active sanction, and before a kick or ban closes the connection. |

## Requests

Every request has 1: request ID. Each is answered by `RequestDone`, by `Error` with the same request ID, or by the result message named in the table.

| Type | Name | Fields | Needs | Answer |
|---|---|---|---|---|
| 0x0510 | MoveUser | 2: user ID, 3: channel ID | `user.move` in both channels, and a higher rank | RequestDone |
| 0x0511 | ApplySanction | 2: user ID (online) or 3: account name, 4: type, 5: channel ID (0 = whole server), 6: duration in seconds (0 = until lifted), 7: reason, 8: also block the address (bool) | depends on the type (below), and a higher rank | RequestDone (result ID: the sanction's ID) |
| 0x0512 | RevokeSanction | 2: sanction ID, 3: reason | `sanction.revoke`, and a higher rank than the target | RequestDone |
| 0x0513 | ListRequest | 2: kind (0 sanctions, 1 reports, 2 appeals, 3 accounts, 4 rules), 3: include closed (bool) | see below | ListResult |
| 0x0514 | ListResult | 2: kind, 3: array of entries of that kind | | |
| 0x0515 | SetUserRole | 2: account name, 3: role ID | `role.assign`; both the account's current role and the new role rank below yours; never Guest or Owner | RequestDone |
| 0x0516 | ReportUser | 2: user ID, 3: reason (required) | anyone; reports share the chat flood limit. Holders of `report.review` who are online get a server message about it. | RequestDone (result ID: the report's ID) |
| 0x0517 | ResolveReport | 2: report ID, 3: outcome (1 action taken, 2 dismissed), 4: note | `report.review` | RequestDone |
| 0x0518 | SubmitAppeal | 2: sanction ID, 3: text | the sanctioned account (not guests); one appeal per sanction | RequestDone |
| 0x0519 | DecideAppeal | 2: appeal ID, 3: decision (1 upheld, 2 reduced, 3 overturned), 4: reason, 5: new duration in seconds (reduced only, counted from when the sanction was given) | `appeal.decide`; not the moderator who gave the sanction, while someone else can decide | RequestDone |
| 0x051A | ReadAudit | 2: entries before this number (0 = newest), 3: how many (1–500) | `audit.read` | AuditPage |
| 0x051B | AuditPage | 2: entries, newest first, 3: chain intact (bool), 4: first damaged entry | | |
| 0x0520 | CreateAccount | 2: name, 3: password (at least 8 characters by default), 4: role ID | `account.manage`; the role must rank below yours and cannot be Guest or Owner | RequestDone |
| 0x0521 | DeleteAccount | 2: name | `account.manage`, and a higher rank than the account (so never the owner, nor yourself). Someone online with that account is disconnected. | RequestDone |
| 0x0522 | SetAccountPassword | 2: name, 3: new password | your own account; or `account.manage` and a higher rank | RequestDone |
| 0x0523 | SaveRole | 2: a role (ID 0 creates one) | `role.manage`; only roles below your rank, with rank 2–89, and only capabilities you hold yourself | RequestDone (result ID: the role's ID) |
| 0x0524 | DeleteRole | 2: role ID | `role.manage`; custom roles below your rank that nobody holds | RequestDone |
| 0x0525 | AddRule | 2: a rule | `role.manage` | RequestDone (result ID: the rule's ID) |
| 0x0526 | DeleteRule | 2: rule ID | `role.manage` | RequestDone |
| 0x0527 | ServerSettingsRequest | 2: settings (omit to only read them) | `server.configure` | ServerSettingsResult |
| 0x0528 | ServerSettingsResult | 2: settings | | |
| 0x0529 | GetServerStats | (none) | `server.monitor` | ServerStatsResult |
| 0x052A | ServerStatsResult | 2: statistics | | |
| 0x052B | TransferOwnership | 2: account name | the owner only | RequestDone |

**Sanction types and what they need:** 0 warning (`user.warn`), 1 voice mute, 2 text mute, 3 mute (`user.mute`), 4 kick (`user.kick`), 5 ban (`user.ban.temporary` up to the server's longest temporary ban; `user.ban.permanent` beyond it or until lifted; `user.ban.address` to also block the address), 6 restriction (`user.mute`). A reason is **required** for bans, restrictions, and mutes longer than one hour. Channel-scoped sanctions need the capability in that channel; server-wide ones need it server-wide.

**Lists:** all sanctions are visible to holders of `user.warn`, `user.mute`, `user.kick`, `user.ban.temporary`, `user.ban.permanent`, `sanction.revoke` or `audit.read` (server-wide); everyone else gets only their own. Addresses are shown only to holders of `user.ban.address`. Reports need `report.review`. Appeals: all of them for `appeal.decide`, otherwise your own. Accounts need `account.manage` or `role.assign`. Rules need `role.manage`. A list holds at most 10,000 entries.

## Entry fields

| Entry | Fields |
|---|---|
| Sanction | 1: ID, 2: type, 3: channel ID (0 = whole server), 4: target's name, 5: target's account (omitted for guests), 6: target's address (only for holders of `user.ban.address`), 7: given by, 8: reason, 9: given at, 10: ends at (0 = until lifted), 11: state (0 active, 1 ended, 2 lifted, 3 overturned), 12: resolved by, 13: resolution |
| Report | 1: ID, 2: reported by, 3: about, 4: their account, 5: reason, 6: time, 7: state (0 open, 1 action taken, 2 dismissed), 8: handled by, 9: note |
| Appeal | 1: ID, 2: sanction ID, 3: from, 4: text, 5: time, 6: state (0 open, 1 upheld, 2 reduced, 3 overturned), 7: decided by, 8: reason |
| Account | 1: name, 2: role ID, 3: disabled (bool) |
| Rule | 1: ID, 2: channel ID (0 = whole server), 3: for an account (bool), 4: account name, 5: role ID, 6: capability, 7: allow (bool) |
| Audit entry | 1: number, 2: time, 3: who, 4: action (for example `sanction.ban`), 5: target, 6: channel ID, 7: reason, 8: details, 9: SHA-256 hash |
| Settings | 1: server name, 2: welcome message, 3: guests allowed, 4: most people online, 5: highest voice bit rate (6,000–510,000), 6: longest temporary ban in days, 7: contact for banned people |
| Statistics | 1: uptime in seconds, 2: people online, 3: connections, 4: channels, 5–7: voice packets in, out and refused, 8: active sanctions, 9: open reports, 10: appeals waiting, 11: audit entries, 12: audit chain intact |

Times are milliseconds since 1970 in UTC. Text fields follow the protocol's text rules; reasons are at most 500 characters.

## What clients are told

- The person sanctioned gets a `SanctionNotice` with the type, scope, end time, reason and who gave it, before a kick or ban closes their connection. The closing `Error` (`Kicked` or `Banned`) repeats the reason and, for bans, the server's contact.
- Nobody is told who reported them.
- Everyone sees mutes and restrictions through the user flags, so their client can show them.
- A client must not reconnect automatically after `Kicked` or `Banned`.
