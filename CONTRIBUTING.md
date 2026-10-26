# Contributing to OpenTalk

Thank you for helping. OpenTalk is meant to be maintained for years, so we value small, well-tested and well-explained changes.

## Before you start

- For anything larger than a bug fix, open an issue first so the design can be discussed.
- Read the [architecture overview](docs/architecture/overview.md) and the [accessibility rules](docs/accessibility/plan.md#design-rules-for-contributors).

## Pull requests

- Keep each pull request focused on one change.
- Build with warnings as errors, run `ctest`, and run `clang-format` on changed files. CI checks all three.
- Add tests for new logic. Any code that parses data from the network or from files needs a fuzz harness.
- Every new control in the UI needs an accessible name, keyboard access, and a line in the [NVDA test script](docs/accessibility/nvda-test-script.md) if it adds a workflow.
- Update documentation in the same pull request, and keep [docs/status.md](docs/status.md) honest. Never document a planned feature as working.
- Significant design decisions get a short ADR in `docs/architecture/decisions/`.
- Adding a dependency or asset? Add it to the [third-party register](docs/licensing/third-party-register.md) first.

## Commit messages

Use a short summary line in the imperative mood (for example "Add jitter buffer statistics"), a blank line, then explain *why* the change is needed.

## Licensing and sign-off

OpenTalk is GPL-3.0-only. New source files start with `// SPDX-License-Identifier: GPL-3.0-only`.

We use the [Developer Certificate of Origin](https://developercertificate.org/). Add a sign-off to each commit (`git commit -s`), which certifies that you have the right to submit the work under the project's license:

```
Signed-off-by: Your Name <you@example.com>
```

Do not submit code, sounds, icons or text copied from TeamTalk or other projects unless its license is compatible and recorded in the register.
