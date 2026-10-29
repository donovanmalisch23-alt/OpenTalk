# Virtual audio driver: feasibility and cost

**Status:** Report for an **owner decision**. No driver work has started. This is the Phase 7 research report that [risk R5](../risk-assessment.md) asks for.

**Recommendation:** do **not** build a driver now. Revisit only if Windows 10 users report that sharing computer audio is unusable for them.

## The question

Some programs, such as Voicemeeter or VB-Audio Virtual Cable, install a **virtual sound card**. Other programs play into it, and an application records from it as if it were a microphone. OpenTalk could ship one, so people could share exactly the sound they choose, on any Windows version, with no feedback.

## What OpenTalk does without a driver (Phase 7)

| Windows | How computer audio is shared | Feedback (others hearing themselves) |
|---|---|---|
| Windows 11, Windows Server 2022 (build 20348 or later) | WASAPI **process loopback**: everything except OpenTalk. | None. OpenTalk's sound is never captured. |
| Windows 10 (1809–22H2) | **Whole-device loopback**, with OpenTalk's own output removed by echo cancellation, and an explicit warning. | Strongly reduced, not guaranteed. Routing OpenTalk to another device (Settings, Audio) removes it entirely. |

People can also install a third-party virtual cable themselves and choose it in OpenTalk's Settings, Audio, as the microphone. That needs no work in OpenTalk. Those cables are proprietary, so OpenTalk cannot ship them.

**Context:** consumer Extended Security Updates for Windows 10 ended in October 2026, and the owner's minimum is still Windows 10 1809. Windows 10 is now the only case the driver would improve.

## What building a driver involves

**Technical work:**

- **A kernel-mode audio driver.** It would be a PortCls/WaveRT miniport, usually started from Microsoft's SysVAD sample in Windows-driver-samples. It exposes one playback endpoint and one capture endpoint and copies between them.
- **Effort:** roughly 2–4 person-months for an experienced Windows driver developer. That covers a basic, reliable cable device and its INF and installer, plus testing on Windows 10 and 11, x64 and ARM64.
- **Maintenance:** it must be re-tested with each Windows feature update.
- **Risk to users:** a bug in a kernel driver crashes the whole computer (a blue screen), not just OpenTalk.

**Signing and distribution:**

- **Microsoft must sign it.** On Windows 10 1607 and later with Secure Boot, kernel drivers only load with a Microsoft signature, through the Hardware Dev Center (Partner Center): attestation signing, or full WHQL with the Hardware Lab Kit.
- **That needs an EV code-signing certificate.** Registering for the Hardware Dev Center requires one, kept on a hardware token or cloud HSM. It costs roughly **US$300–700 per year**, depending on the vendor. *Check current prices and Microsoft's current requirements before deciding.* The same certificate would also sign OpenTalk's own programs and reduce SmartScreen warnings, which is part of the separate code-signing decision.
- **Installing needs administrator rights.** That conflicts with the portable edition, which must work without installation. The driver would have to be an optional extra in the installer.

**Licensing:**

- SysVAD is under the **Microsoft Public License (MS-PL)**, which the FSF considers incompatible with the GPL.
- A driver derived from SysVAD would therefore be a **separate program** under MS-PL, shipped alongside GPL-3.0-only OpenTalk but not linked with it.
- This needs the same legal review as the plugin licensing policy before any release.
- Writing the driver from scratch avoids MS-PL but costs considerably more.

## Cost summary

| Item | One-off | Yearly |
|---|---|---|
| Driver development (2–4 person-months) | Development time | — |
| EV code-signing certificate | — | about US$300–700 |
| Hardware Lab Kit testing machines (for WHQL; optional for attestation) | Test hardware | — |
| Maintenance: re-test on Windows updates, fix driver bugs | — | Days to weeks |
| Legal review (MS-PL driver beside GPL software) | Review fee or volunteer time | — |

## Options for the owner

1. **No driver (recommended for now).** Windows 11 users get feedback-free sharing. Windows 10 users get echo-cancelled sharing with a warning, or route OpenTalk to another device. Cost: nothing.
2. **Document third-party cables.** Explain in the user guide how to use a cable a person installs themselves. Cost: documentation only. This can be combined with option 1.
3. **Build and sign a driver.** As above: the highest cost, kernel-level risk, an installer-only feature, and a licensing review.

**To decide:** choose option 1, 2 or 3. If 3, also decide the certificate vendor and budget, and whether the driver is derived from SysVAD (MS-PL) or written from scratch.
