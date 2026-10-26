# Security policy

## Reporting a vulnerability

Please **do not** open a public issue for security problems.

Report privately through GitHub: on the repository's **Security** tab, choose **Report a vulnerability**. Include the affected version or commit, steps to reproduce, and the impact you expect.

We aim to acknowledge reports within 7 days and to agree on a disclosure date with you. We credit reporters who want credit.

## Supported versions

OpenTalk has not had a release yet. Until version 1.0, only the latest commit on the default branch receives fixes.

## Scope

Examples of security problems: anything that lets someone read or alter traffic they should not, bypass server-side permissions, crash or exhaust a server remotely, run code through crafted input or plugins beyond their granted capabilities, or extract credentials.

What OpenTalk does and does not protect against is described in the [threat model](docs/security/threat-model.md). In particular, OpenTalk does **not** provide end-to-end encryption: the server operator can access the text and audio their server relays.
