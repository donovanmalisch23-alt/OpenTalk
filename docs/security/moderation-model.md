# Moderation and permissions model

**Status:** Designed (Phase 5). Phase 3 implemented a first step: three fixed roles (guest, user, administrator) enforced by `ServerCore`, with the permission table in the [protocol document](../protocol/proposal.md#login-channels-presence-and-text-implemented-phase-3). The full model below replaces those fixed roles. It is a pure-logic component (`src/moderation`) that the server calls for every decision, so it can be tested exhaustively without networking.

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
| Channel Moderator | Moderation inside the channels where it is assigned |
| Registered User | Join, talk, send text, create temporary channels |
| Guest | Join and talk where allowed |
| Restricted | Join only; cannot talk or send text |

**Non-delegable owner capabilities:** transferring ownership, deleting the server's audit log, and changing who holds the Owner role. Nobody else can hold these, so an administrator cannot lock out the owner.

**Role ranks:** every role has a rank. A user can act on, or assign roles to, only users and roles of **strictly lower** rank. This stops moderators sanctioning their peers or superiors.

## How permissions resolve

For a user U, capability C and channel X:

1. If U holds the Owner role, **allow**.
2. Walk from X up through its parent channels to the server level. At each level, look at the rules for C that apply to U: rules for U's account, then rules for each of U's roles.
3. At the **first level that has any matching rule**: if any matching rule is **Deny**, the result is **deny**; otherwise it is **allow**. Lower levels are not consulted.
4. If no level has a rule, use the server-wide grants of U's roles (allow if any role grants C).
5. Otherwise **deny**. The default is deny.

So: the most specific level wins, deny beats allow at the same level, and everything is denied by default. The resolver can also return *why* a decision was made ("denied by rule on channel Lobby for role Guest"). Administrators can see that explanation in the UI.

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
- Readable with `audit.read`. Retention is configurable, with a documented default of 1 year.

## Reports and appeals

- **Reports:** any user can report a user or message with a reason. A report is visible to holders of `report.review`, and the reported user does not see who filed it.
- **Appeals:** a sanctioned user can appeal once per sanction. The appeal goes to holders of `appeal.decide`. **The moderator who issued the sanction cannot decide its appeal** when anyone else can. Decisions (upheld, reduced, overturned) are logged and the user is told the outcome.

## Recovery

`opentalk-server --data-dir <path> --recover-owner <username>` works only offline, with the server stopped, and needs file-system access to the data directory. It is logged in the audit log on next start.

## What OpenTalk cannot guarantee

OpenTalk provides enforcement, transparency and accountability tools. It cannot make every server owner fair. Owners decide their own policies within these mechanisms.
