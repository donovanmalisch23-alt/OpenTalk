# Licensing policy

> This document records engineering policy. It is not legal advice. Get qualified legal review before the first public binary release.

## First-party code

GPL-3.0-only ([ADR 0002](../architecture/decisions/0002-license-gpl-3-only.md)). Every source file starts with:

```
// SPDX-License-Identifier: GPL-3.0-only
```

The full license text is in [`LICENSE`](../../LICENSE). Contributions use the Developer Certificate of Origin; see [CONTRIBUTING.md](../../CONTRIBUTING.md).

## Accepting a dependency or asset

Before merging anything third-party (library, codec, font, icon, sound, specification text), add a row to the [third-party register](third-party-register.md) with these fields:

- component and version
- author or publisher, and source URL
- license (SPDX ID)
- whether modification and redistribution are permitted
- attribution requirements
- GPL-3.0-only compatibility
- open questions

A reviewer checks the row. Unknown or unclear licences block the merge.

**Compatible with GPL-3.0-only:** public domain, MIT, BSD, ISC, zlib, Apache-2.0, LGPL-2.1+ and LGPL-3.0, GPL-3.0, and "GPL-2.0 or later".
**Not compatible:** GPL-2.0-only, proprietary, non-commercial, and "no derivatives" licences.

## Qt

Qt open-source modules used here are available under LGPL-3.0 (and GPL). Because OpenTalk itself is GPL-3.0-only, using Qt under either licence is compatible. For binary distribution:

1. **Link Qt dynamically**, as `windeployqt` does, so users can replace the Qt libraries. That is the LGPL relinking requirement.
2. **Ship licence texts and notices** for Qt and for its bundled third-party code (for example PCRE2, HarfBuzz, libpng and FreeType, depending on build). Qt ships these as attribution files.
3. **Make the corresponding Qt source available** (an exact-version download link or an offer), with any modifications. OpenTalk does not plan to modify Qt.
4. **Check each module's licence** before adding it. A few Qt add-on modules are GPL-3.0 or commercial only. That is acceptable for OpenTalk but must be recorded.

**Help → About Qt** in the application shows Qt's own licence information.

## TeamTalk

TeamTalk 5 is a functional reference only. Do not copy its source code, protocol implementation, icons, sounds or text. **TeamTalk soundpacks are not bundled.** OpenTalk will ship original sounds. Importing user-supplied soundpacks is allowed when the user has the right to use them; a user's own files are their responsibility.

## Codec patents

Opus is covered by royalty-free patent licences from its contributors. Any other codec needs a patent review before adoption.
