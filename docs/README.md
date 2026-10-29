# OpenTalk documentation

Every document marks features with one of these states, so nothing planned is mistaken for something that works:

| State | Meaning |
|---|---|
| **Designed** | Written down here; no code yet. |
| **Implemented** | Code exists in the repository. |
| **Built** | Compiles in CI on the stated platform. |
| **Unit-tested** | Automated tests cover it and pass. |
| **Integration-tested** | Tested together with other components. |
| **Manually verified** | A person checked it, for example with NVDA. |
| **Release-ready** | Meets the release checklist. |

## Start here

- [Project brief: summary and requirements](project-brief.md)
- [Current status and verification report](status.md)
- [Release notes for 1.0.0-rc.1](release-notes/1.0.0-rc.1.md) and its [acceptance checklist](release-notes/1.0.0-rc.1-acceptance.md)
- [Roadmap](roadmap.md)
- [Risk assessment](risk-assessment.md)

## Design

- [Architecture overview](architecture/overview.md) and [decision records](architecture/decisions/)
- [Data layout](architecture/data-layout.md)
- [Dependency evaluation](dependencies.md)
- [Protocol proposal](protocol/proposal.md), [voice transport](protocol/voice.md) and [moderation messages](protocol/moderation.md)
- [Accessibility plan](accessibility/plan.md) and [NVDA test script](accessibility/nvda-test-script.md)
- [Threat model and security plan](security/threat-model.md)
- [Moderation and permissions model](security/moderation-model.md)
- [Licensing policy](licensing/policy.md) and [third-party register](licensing/third-party-register.md)

## Using and extending

- [Getting started: installing, connecting, remembered passwords, status messages, moving settings](user-guide/getting-started.md)
- [Voice and sounds (user guide)](user-guide/voice-and-sounds.md)
- [Reports, sanctions and appeals (user guide)](user-guide/moderation.md)
- [Plugins (user guide)](user-guide/plugins.md)
- [Troubleshooting](user-guide/troubleshooting.md)
- [Server quick start](server-administration/quick-start.md) and [hosting for others](server-administration/hosting.md): firewalls, systemd, Docker, certificates, backups, upgrades
- [Moderating and administering a server](server-administration/moderation.md)
- [Audio benchmarks](audio/benchmarks.md)
- [SDK guide](sdk/README.md) (bots and tools)
- [Writing a plugin](plugins/developer-guide.md), the [plugin message reference](plugins/ipc.md) and the [licensing policy for plugins and bots](plugins/licensing.md)

## Development

- [Building and testing](development/building.md)
- [Making a release](development/releasing.md)
- [Signing releases: options for the owner](development/release-signing.md)
- [Client settings reference](reference/client-settings.md)
