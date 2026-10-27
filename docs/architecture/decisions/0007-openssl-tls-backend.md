# ADR 0007: Qt's OpenSSL TLS backend on every platform, including Windows

**Status:** Accepted

**Context:** The owner set **Windows 10** as the minimum supported version. OpenTalk requires TLS 1.3 for the control channel ([ADR 0005](0005-transport-and-serialization.md)). Qt on Windows uses Schannel (the TLS built into Windows) by default, but Schannel supports TLS 1.3 only on Windows 11 and Windows Server 2022 and later, not on Windows 10. DTLS for the future audio channel (`QDtls`) is also believed to need the OpenSSL backend.

**Options:**

1. Schannel, allowing TLS 1.2 on Windows 10.
2. Schannel on Windows 11, OpenSSL on Windows 10.
3. Qt's OpenSSL backend everywhere, shipping OpenSSL 3 with the Windows build.

**Decision:** Option 3. `security::initializeTls()` selects the `openssl` backend, then checks that TLS 1.3 is available, and refuses to run otherwise with a clear message.

**Why:**

- Every user gets the same TLS 1.3 behaviour regardless of Windows version, with one code path to test.
- It is the same backend as on Linux, so Linux CI exercises the real TLS stack.
- It leaves the door open for DTLS.
- Option 1 weakens security for a supported platform. Option 2 doubles the test matrix.

**Consequences:**

- The Windows package includes `libssl-3-x64.dll` and `libcrypto-3-x64.dll` (Apache-2.0, compatible with GPL-3.0-only; recorded in the third-party register).
- **OpenSSL security updates become OpenTalk's job.** Each OpenSSL security release must trigger an OpenTalk update.
- **Version caveat:** CI currently uses the OpenSSL that Qt's installer provides (3.0.16). The OpenSSL 3.0 LTS series reached end of life in September 2026. Before the first public release, the packaged OpenSSL must move to a supported series (3.5 LTS), built or obtained from a trusted, pinned source. This is tracked in the roadmap (Phase 8, M8.x "supported OpenSSL in packages").
- The system certificate store is still used for servers that have certificates from public authorities. Qt's OpenSSL backend on Windows reads the Windows root store.

**Validation:** Integration tests run TLS 1.3 end to end on Linux and Windows CI. A test confirms that a TLS 1.2-only client is refused.
