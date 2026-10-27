# Third-party register

One row per third-party component or asset that is **used by or shipped with** OpenTalk. See the [policy](policy.md) for the process. Candidates under evaluation are listed in [dependencies.md](../dependencies.md), not here.

| Component | Version | Publisher / source | License (SPDX) | Modify / redistribute | Attribution | GPL-3.0-only compatible | Open questions |
|---|---|---|---|---|---|---|---|
| Qt Core, Gui, Widgets | 6.8.3 (Windows CI), 6.4+ supported | The Qt Company, qt.io | LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only | Yes / Yes | Licence texts and Qt third-party attributions in binary distributions | Yes | Exact attribution files `windeployqt` should ship (Phase 8 packaging task). |
| Qt Network (incl. the OpenSSL TLS plugin) | Same as Qt | The Qt Company | Same as Qt | Yes / Yes | As for Qt | Yes | — |
| OpenSSL (libssl, libcrypto) | Windows: 3.6.5 from vcpkg (pinned); Linux: system 3.0.x | OpenSSL Project, openssl.org | Apache-2.0 | Yes / Yes (Windows package) | Ship the licence text and notices | Yes (Apache-2.0 into GPLv3) | **3.6 support ends around November 2026; the release package must use a supported series** (an LTS such as 3.5 at a current patch level, built from source if vcpkg lacks it) ([ADR 0007](../architecture/decisions/0007-openssl-tls-backend.md)). Licence files are not yet in the zip (Phase 8 packaging). |
| libsodium | Windows: 1.0.22 from vcpkg (pinned); Linux: system 1.0.18 or later | Frank Denis, libsodium.org | ISC | Yes / Yes (Windows package) | Ship the licence text | Yes | Licence file not yet in the zip (Phase 8). |
| SQLite (inside Qt's `qsqlite` driver) | As bundled by Qt 6.8.3 (Windows); system on Linux | SQLite project, sqlite.org | Public domain (blessing) | Yes / Yes | None required | Yes | — |
| Qt SQL | Same as Qt | The Qt Company | Same as Qt | Yes / Yes | As for Qt | Yes | — |
| Qt Test | Same as Qt | The Qt Company | Same as Qt | Yes / Not shipped | — | Yes | Test-only. |
| GNU GPL v3 text (`LICENSE`) | 29 June 2007 | Free Software Foundation | Verbatim copying permitted | No modification / Yes | — | n/a | — |

No fonts, icons or sounds are bundled yet.
