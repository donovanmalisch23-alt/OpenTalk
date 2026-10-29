# Signing releases: options for the owner

**Status:** a decision for the owner. The owner deferred it to "before Phase 8" (2026-10-26), and it has **not been made yet**.

Until a decision is made, releases follow the "now" column below. They are unsigned, so Windows SmartScreen warns about them, but their integrity can still be checked.

## The problem

Windows shows "Windows protected your PC" (SmartScreen) for programs that are not code-signed, or are signed with a certificate that has no reputation yet. A screen reader user must then find "More info" and "Run anyway", which is a poor first impression and teaches people to click through warnings.

Code signing also protects against a tampered download (threat T13 in the [threat model](../security/threat-model.md)).

## What every release has, signed or not

- **`SHA256SUMS.txt`** next to the files on the Releases page. `Get-FileHash <file>` in PowerShell shows a file's checksum to compare.
- **GitHub build provenance (artifact attestations).** The release workflow records, signed by GitHub, which workflow run built each file from which commit. It is free for public repositories. Anyone can check a download with:

  ```sh
  gh attestation verify <file> --repo donovanmalisch23-alt/opentalk
  ```

  This shows the file was built by the project's own CI, not uploaded by someone else, but it does not change what SmartScreen shows.

## Options

| Option | Cost (check current prices) | SmartScreen | Notes |
|---|---|---|---|
| **A. Unsigned, with checksums and provenance** (now) | Free | Warns on every new version | Honest, and the integrity checks above still work. |
| **B. Microsoft Trusted Signing** (Azure "Artifact Signing") | About US$10 per month | Reputation builds as people download; usually no warning after that | Needs an Azure subscription and an identity check (for individuals, availability depends on the country). Signs in the GitHub workflow, with no hardware token. |
| **C. OV code-signing certificate** | About US$200–400 per year | Reputation must build up, as for B | The key must be on a hardware token or cloud HSM, which complicates CI signing. |
| **D. EV code-signing certificate** | About US$300–700 per year | Builds reputation fastest | Needed anyway if OpenTalk ever ships a kernel driver ([virtual driver report](../audio/virtual-driver-feasibility.md)). Hardware token or cloud HSM. |
| **E. SignPath Foundation** (free signing for open-source projects) | Free if accepted | As for C | Apply with the project; releases are signed by their service from CI artifacts, under their certificate. |

**Recommendation:**

- **Now:** start with A for the first release.
- **Next:** apply to E, or set up B, as soon as there are users, because both suit a CI-built open-source project.
- **Later:** choose D only if the virtual audio driver is approved.

**To decide:** pick one option (or "A for now"). For B, C or D, also choose who holds the account or certificate.

## What changes once there is a certificate

The release workflow signs `OpenTalk.exe`, the other programs, the DLLs OpenTalk builds itself, the installer and its uninstaller, with SHA-256 and a timestamp. CI then checks the signatures with `signtool verify /pa`. Nothing else in the build changes.
