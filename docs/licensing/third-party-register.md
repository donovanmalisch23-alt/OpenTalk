# Third-party register

One row per third-party component or asset that is **used by or shipped with** OpenTalk. See the [policy](policy.md) for the process. Candidates under evaluation are listed in [dependencies.md](../dependencies.md), not here.

| Component | Version | Publisher / source | License (SPDX) | Modify / redistribute | Attribution | GPL-3.0-only compatible | Open questions |
|---|---|---|---|---|---|---|---|
| Qt Core, Gui, Widgets | 6.8.3 (Windows CI), 6.4+ supported | The Qt Company, qt.io | LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only | Yes / Yes | Licence texts and Qt third-party attributions in binary distributions | Yes | Exact attribution files `windeployqt` should ship (Phase 8 packaging task). |
| Qt Test | Same as Qt | The Qt Company | Same as Qt | Yes / Not shipped | — | Yes | Test-only. |
| GNU GPL v3 text (`LICENSE`) | 29 June 2007 | Free Software Foundation | Verbatim copying permitted | No modification / Yes | — | n/a | — |

No fonts, icons or sounds are bundled yet.
