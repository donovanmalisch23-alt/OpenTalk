# Licensing of plugins, bots and other programs

> **Draft engineering policy, not legal advice.** It records the project's intent and reasoning. **It needs review by a qualified lawyer before the first public release** (an open item for the project owner). Where it is uncertain, it says so.

OpenTalk is GPL-3.0-only. Programs that work with it fall into three groups, which are treated differently because they connect to OpenTalk differently.

| Kind | How it connects | Licence it must use |
|---|---|---|
| **Out-of-process plugin** ([developer guide](developer-guide.md)) | A separate program. It exchanges documented JSON messages with the app over a pipe, and links no OpenTalk code. | **Any licence**, including proprietary, is the project's position (see below). |
| **Program using the SDK** (bots, tools) | Links the `OpenTalk::Sdk` library, which is GPL-3.0-only. | **GPL-3.0-compatible** terms when distributed, because it is a combined work with GPL code. |
| **Program speaking the network protocol itself** | Its own implementation of the [protocol](../protocol/proposal.md), linking no OpenTalk code. | Any licence. |
| **First-party plugins and examples** (in this repository) | Either of the above. | GPL-3.0-only, like all OpenTalk code. |

## Why out-of-process plugins may use any licence

The Free Software Foundation's own guidance is that pipes, sockets and command-line arguments are normally how *separate* programs communicate, so the GPL does not extend to the other program, but that if the programs exchange complex internal data structures closely enough to form a single combined program, it may ([GPL FAQ: "MereAggregation"](https://www.gnu.org/licenses/gpl-faq.html#MereAggregation)). OpenTalk's plugin interface was designed to be on the "separate programs" side:

- The plugin runs in its own process and links no OpenTalk code or headers.
- The messages are a small, documented, versioned public interface (JSON), not internal data structures.
- The same interface works for programs in any language.

So the project's position is that out-of-process plugins are separate works and their authors choose their own licence. **This is the uncertain part that legal review must confirm.** If review disagrees, this page will change before release, and plugin authors will be told in the changelog.

## Rules that apply to every plugin

- A plugin must not include OpenTalk source code, or copy OpenTalk's code into itself, unless it is GPL-3.0-compatible.
- A plugin that bundles third-party libraries must follow their licences, and should list them in its folder (for example in a `NOTICE` file).
- The plugin's `license` field must state its licence truthfully; it is shown to users before they turn it on.
- OpenTalk does not distribute third-party plugins. Each author distributes their own.

## Not yet decided

- **In-process plugins** (shared libraries loaded into the app, for real-time audio effects) would be combined works and therefore GPL-3.0-compatible. None exist yet; Phase 7 implements effects inside OpenTalk instead.
- Whether the SDK should later move to a more permissive licence (for example LGPL-3.0) so that proprietary bots can use it. That is the copyright holders' decision.
