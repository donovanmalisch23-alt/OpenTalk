# Making a release

Releases are built and published by CI, never from someone's own computer, so every file on the Releases page comes from a known commit and passed every test.

## What a release contains

The `release` job in [`.github/workflows/ci.yml`](../../.github/workflows/ci.yml) runs only for a tag that starts with `v`, and only after the Windows, Linux, fuzzing and container jobs passed for that commit. It publishes:

- `OpenTalk-<version>-portable-windows-x64.zip`: the portable folder the Windows job built and smoke-tested, inside one top-level folder.
- `OpenTalk-<version>-setup-x64.exe`: the installer the Windows job built, installed and uninstalled.
- `SHA256SUMS.txt`, and GitHub build-provenance attestations for both files ([signing options](release-signing.md)).
- The release text from `docs/release-notes/<version>.md`. A version with a hyphen (`1.0.0-rc.1`) is marked as a pre-release.

## Steps

1. **Version.** In the top-level `CMakeLists.txt`, set `VERSION` (numbers only) and `OPENTALK_VERSION_LABEL` (for example `rc.1`, or empty for a final release). The app, the server and the installer all show `<VERSION>-<label>`.
2. **Notes.** Write `docs/release-notes/<version>.md` for people downloading it: what it is, the files, what was and was not checked, known limitations. Update the acceptance checklist and `docs/status.md`. Move the changelog's "Unreleased" entries under the new version.
3. **Checks before tagging.** On Linux: the debug and AddressSanitizer test suites, clang-tidy, formatting, and a few minutes of each fuzzer (the commands are in [status](../status.md#commands-that-were-run-linux-sandbox-ubuntu-2404)). CI must be green on the commit to tag.
4. **For a final release** (no label), also: the [NVDA test script](../accessibility/nvda-test-script.md) on Windows 10 and 11, and a voice test with real sound devices.
5. **Tag and push:**

   ```sh
   git tag -a v1.0.0-rc.1 -m "OpenTalk 1.0.0-rc.1"
   git push origin v1.0.0-rc.1
   ```

   The release job checks that the tag matches the version in `CMakeLists.txt` and that the notes file exists, and stops if not.

   **Or on GitHub's website** (no command line needed): Releases, "Draft a new release", type the tag (`v1.0.0-rc.1`) and choose "Create new tag on publish", pick the branch or commit as the target, tick "Set as a pre-release" for a release candidate, and publish. CI then builds and tests that commit and attaches the files and the release notes to the release, in about 25 minutes. Until then the release shows only GitHub's source archives.
6. **Check the release page:** both files and `SHA256SUMS.txt` are there; download the zip, compare its checksum, and start it on a clean Windows machine.

## If something goes wrong

- **A job failed:** fix it on the branch, then delete the tag (`git push origin :refs/tags/v1.0.0-rc.1`, and `git tag -d v1.0.0-rc.1`) and tag the fixed commit. Nothing was published, because the release job runs last.
- **A published release is broken:** do not replace its files. Mark it as such in its text, and release a new version (for example `rc.2`).
