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
- [Roadmap](roadmap.md)
- [Risk assessment](risk-assessment.md)

## Design

- [Architecture overview](architecture/overview.md) and [decision records](architecture/decisions/)
- [Data layout](architecture/data-layout.md)
- [Dependency evaluation](dependencies.md)
- [Protocol proposal](protocol/proposal.md)
- [Accessibility plan](accessibility/plan.md) and [NVDA test script](accessibility/nvda-test-script.md)
- [Threat model and security plan](security/threat-model.md)
- [Moderation and permissions model](security/moderation-model.md)
- [Licensing policy](licensing/policy.md) and [third-party register](licensing/third-party-register.md)

## Using and extending

- [Server quick start](server-administration/quick-start.md)
- [SDK guide](sdk/README.md)

## Development

- [Building and testing](development/building.md)
- [Client settings reference](reference/client-settings.md)
