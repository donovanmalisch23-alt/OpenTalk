# Third-party register

One row per third-party component or asset that is **used by or shipped with** OpenTalk. See the [policy](policy.md) for the process. Candidates under evaluation are listed in [dependencies.md](../dependencies.md), not here.

| Component | Version | Publisher / source | License (SPDX) | Modify / redistribute | Attribution | GPL-3.0-only compatible | Open questions |
|---|---|---|---|---|---|---|---|
| Qt Core, Gui, Widgets | 6.8.3 (Windows CI), 6.4+ supported | The Qt Company, qt.io | LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only | Yes / Yes | Licence texts and Qt third-party attributions in binary distributions | Yes | Exact attribution files `windeployqt` should ship (Phase 8 packaging task). |
| Qt Network (incl. the OpenSSL TLS plugin) | Same as Qt | The Qt Company | Same as Qt | Yes / Yes | As for Qt | Yes | — |
| OpenSSL (`libssl-3-x64.dll`, `libcrypto-3-x64.dll`, Windows package only) | 3.0.16 from the Qt installer (CI) | OpenSSL Project, openssl.org | Apache-2.0 | Yes / Yes | Ship the licence text and notices | Yes (Apache-2.0 into GPLv3) | **3.0 is end of life (Sept 2026); move to 3.5 LTS before release** ([ADR 0007](../architecture/decisions/0007-openssl-tls-backend.md)). Licence and notice files are not yet in the zip (Phase 8 packaging). |
| Qt Test | Same as Qt | The Qt Company | Same as Qt | Yes / Not shipped | — | Yes | Test-only. |
| GNU GPL v3 text (`LICENSE`) | 29 June 2007 | Free Software Foundation | Verbatim copying permitted | No modification / Yes | — | n/a | — |

No fonts, icons or sounds are bundled yet.
