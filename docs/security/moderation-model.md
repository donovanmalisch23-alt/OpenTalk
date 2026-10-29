# Moderation and permissions model

**Status:** implemented (Phase 5) and tested, with the exceptions listed under [Not yet implemented](#not-yet-implemented). The rules live in a pure-logic library (`src/moderation`) that the server calls for every decision, so they are tested without networking (`tst_moderation`), across every request and role (`tst_authorization`, 182 cases), through the server (`tst_moderation_server`) and in the desktop app (`tst_moderation_ui`). The messages are in [protocol/moderation.md](../protocol/moderation.md); how to use it is in the [server administration guide](../server-administration/moderation.md).

## Identities

- Each account has a permanent numeric **user ID**. Display names are not unique and are never used for security decisions.
- Guests get a session-only identity. Bots are ordinary accounts marked as bots.

## Capabilities and roles

A **capability** is one permission, for example `channel.join`, `channel.create`, `text.send`, `voice.transmit`, `user.mute`, `user.kick`, `user.ban.temporary`, `user.ban.permanent`, `role.assign`, `server.configure`, `audit.read`, `appeal.decide`, `broadcast.send`.

A **role** is a named set of capabilities. The default roles are listed below; owners can edit and add roles.

| Default role | Typical capabilities |
|---|---|
| Owner | Everything, including the non-delegable capabilities below |
| Administrator | Server configuration, roles below Administrator, all moderation actions |
| Moderator | Warn, mute, kick, temporary ban, read audit log, decide appeals |
| Channel Moderator | Moderation inside the channels where it is assigned. Not a built-in role: it is made with channel permission rules for an account or a role (see the [administration guide](../server-administration/moderation.md#channel-moderators)). |
| Registered User | Join, talk, send text, create temporary channels |
| Guest | Join and talk where allowed |
| Restricted | Join only; cannot talk or send text |

**Non-delegable owner capabilities:** transferring ownership, deleting the server's audit log, and changing who holds the Owner role. Nobody else can hold these, so an administrator cannot lock out the owner. (The audit log cannot be deleted through OpenTalk at all; old entries are removed only by the retention period.)

**Built-in ranks:** Owner 100, Administrator 90, Moderator 50, Registered user 10, Guest 5, Restricted 1. Custom roles take ranks from 2 to 89 and, when created by someone other than the owner, below the creator's own rank. A role can only grant capabilities its creator holds.

**Role ranks:** every role has a rank. A user can act on, or assign roles to, only users and roles of **strictly lower** rank. This stops moderators sanctioning their peers or superiors.

## How permissions resolve

For a user U, capability C and channel X:

1. If U holds the Owner role, **allow**.
2. Walk from X up through its parent channels to the server level. At each level, first look at the rules for C that name U's account; if there are none, look at the rules for U's role.
3. The **first set of matching rules** decides: if any of them is **Deny**, the result is **deny**; otherwise it is **allow**. Nothing further is consulted. (So "registered users may not talk in the Lobby, but alice may" works: the rule for alice's account is more specific than the rule for her role.)
4. If no level has a rule, use the server-wide grants of U's roles (allow if any role grants C).
5. Otherwise **deny**. The default is deny.

So: the most specific level wins, an account rule beats a role rule at the same level, deny beats allow among equally specific rules, and everything is denied by default. The resolver can also return *why* a decision was made ("denied by rule on channel Lobby for role Guest"). Administrators can see that explanation in the UI.

## Sanctions

| Field | Notes |
|---|---|
| Type | Warning, mute (voice, text or both), kick, temporary ban, permanent ban, restriction |
| Scope | Server-wide or one channel |
| Target | User ID; optionally an IP address or range for bans, which needs a separate capability |
| Actor | User ID of the moderator or bot |
| Reason | **Required** for bans, mutes longer than 1 hour, and restrictions; optional otherwise |
| Created / expires | UTC timestamps. The UI shows local time with the time zone. Temporary sanctions are lifted automatically by the server. |
| State | Active, expired, revoked (with who and why), overturned on appeal |

Affected users are told the type, scope, duration and reason, unless a server policy hides the reason for a documented safety purpose. They are not told the identity of a person who reported them.

**Mass-action safeguard:** any action that affects more than 5 users at once (a configurable number) needs a confirmation step and a reason, and is written to the audit log as one batch.

## Audit log

- Append-only. Each entry records the time, actor, action, target, scope, reason and result.
- **Tamper-evident:** each entry stores a hash of the previous entry, and the server checks the chain on start-up. Someone with direct file access can rewrite the database. The hash chain shows that a change happened but cannot prevent it. That limit is documented. Owners can export the log for off-machine copies.
- Readable with `audit.read`. Retention is configurable (`--audit-retention-days`), with a default of 1 year; old entries are removed at start-up and once a day. The oldest remaining entry keeps the hash of the entry before it, so the rest of the chain can still be checked.

## Reports and appeals

- **Reports:** any user can report a user or message with a reason. A report is visible to holders of `report.review`, and the reported user does not see who filed it.
- **Appeals:** a sanctioned user can appeal once per sanction. The appeal goes to holders of `appeal.decide`. **The moderator who issued the sanction cannot decide its appeal** when anyone else can. Decisions (upheld, reduced, overturned) are logged and the user is told the outcome.

## Recovery

`opentalk-server --data-dir <path> --recover-owner <username>` works only offline, with the server stopped, and needs file-system access to the data directory. It is logged in the audit log on next start.

## What OpenTalk cannot guarantee

OpenTalk provides enforcement, transparency and accountability tools. It cannot make every server owner fair. Owners decide their own policies within these mechanisms.

## Not yet implemented

| Item | State |
|---|---|
| Mass-action safeguard | Not needed yet: every Phase 5 action affects one person. It will come with the first action that affects many (for example moving a whole channel). |
| Bans on IP address **ranges** | Only exact addresses can be blocked. |
| Reporting a single **message** | Reports are about a person; the reason can quote the message. |
| Hiding the reason from the sanctioned person by policy | Not available; reasons are always shown. |
| Bots marked as bots | Arrives with bots in Phase 6. |

The client shows only the actions a person may use, from the capabilities the server sends (server-wide, plus those held only in some channels). That is a convenience: the server checks every request again, and a refusal names the capability and the rule that decided it, for example "user.kick: denied by a rule on channel 1000 for role Registered user".
