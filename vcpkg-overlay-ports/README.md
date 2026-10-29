# vcpkg overlay ports

Ports here replace vcpkg's own for OpenTalk's Windows builds (see `vcpkg-configuration.json`).

| Port | Why | Source |
|---|---|---|
| `openssl` | The release package must use a supported OpenSSL series. At OpenTalk's pinned vcpkg baseline, vcpkg offers 3.6.x (support ends around November 2026) and an old 3.5 patch release. This port builds the newest **3.5 LTS** release (supported until April 2030) from the official tarball. | vcpkg's `openssl` port as of its 3.5.4 entry, with only the source download, version and checksum changed. All of vcpkg's patches apply unchanged. |

**Updating OpenSSL:**

1. Download `openssl-3.5.N.tar.gz` from https://www.openssl.org/source/.
2. Check its SHA-256 against the one published there.
3. Put the file's SHA-512 (`sha512sum`) and the new version into `openssl/portfile.cmake` and `openssl/vcpkg.json`.
4. Check that the patches still apply (`git apply --check` in the unpacked source), then let CI build it.

Record the new version in [the third-party register](../docs/licensing/third-party-register.md).
