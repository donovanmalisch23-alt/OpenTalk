# ADR 0009: miniaudio for audio devices; Opus for coding

**Status:** Accepted (Phase 4)

**Context:** The client needs low-latency capture and playback with device selection, recovery when a device disappears, and later **loopback capture of other programs' sound without capturing OpenTalk itself** (to share computer audio without feedback). Windows is first; Linux and macOS come later. The roadmap planned a spike comparing miniaudio, Qt Multimedia and raw WASAPI.

**Options:**

1. **Qt Multimedia** (`QAudioSource`/`QAudioSink`). Already a Qt module. However, its latency is hard to control (buffer sizes are hints), it has no loopback capture, and its Windows backend has changed between Qt versions.
2. **Raw WASAPI.** Lowest level and full control, including process loopback. But it is Windows-only and a large amount of COM code to maintain, and every later platform needs another backend.
3. **miniaudio** (single C header, public domain or MIT-0). It wraps WASAPI, Core Audio, ALSA, PulseAudio and others behind one API, with low-latency settings. On Windows it exposes **process-excluding loopback** (`wasapi.loopbackProcessID` with `loopbackProcessExclude`), which is exactly what system-audio sharing needs.
4. PortAudio. Mature, but no process-loopback support and a heavier build.

**Decision:** Option 3, behind OpenTalk's own `AudioBackend` interface, so a raw WASAPI backend can be added later if measurements demand it.

- **Version:** miniaudio 0.11.25, vendored in `third_party/miniaudio` with its licence. Its SHA-256 is in the [third-party register](../../licensing/third-party-register.md).
- **Amendment (October 2026):** fuzzing found an out-of-bounds read in miniaudio's FLAC seeking and a hang in its WAV reader. The vendored copy carries a small patch for each (`third_party/miniaudio/opentalk-flac-seek.patch` and `opentalk-wav-fmt-chunk.patch`), recorded in the register, until upstream fixes them. Any other change to the vendored file needs the same: a patch file, a register entry and a test.
- **Opus** (libopus, BSD-3-Clause) is the only codec: 48 kHz mono, 20 ms frames, in-band forward error correction and packet-loss concealment. It comes from vcpkg on Windows and the system on Linux.

**Why:**

- One small, permissively licensed dependency covers every target platform and the hardest Windows feature (process-excluding loopback).
- Device I/O happens on miniaudio's real-time thread, which only copies samples to and from lock-free ring buffers. All encoding, decoding, mixing and jitter buffering run on OpenTalk's own audio thread, where they can be tested with a **synthetic backend** in CI, where there are no sound cards.

**Spike results, and what is not measured yet:**

- **Measured:** on the CI and development machines (no audio hardware), the full software path. That is the synthetic device → Opus → jitter buffer → mixer, both in one process and across a real server over UDP. The benchmark tool reports per-frame CPU cost and the latency added by buffering; see [docs/audio/benchmarks.md](../../audio/benchmarks.md).
- **Not measured:** end-to-end latency through real sound cards and Windows audio drivers. This needs a person with a Windows machine and a loopback cable or two machines, using the procedure in the benchmark document. Until then, device latency is the theoretical figure: miniaudio was asked for 10 ms periods, and WASAPI shared mode typically adds 10–20 ms.
- **Not compared yet:** Qt Multimedia and raw WASAPI. They were not built, so the comparison is based on documentation and features, not measurements.

**Consequences:**

- The miniaudio code is compiled as C, without OpenTalk's warning flags. It is a vendored dependency to update deliberately: check its release notes and re-run the audio tests.
- A device that disappears makes the engine retry every 3 seconds, and the UI reports the problem.
