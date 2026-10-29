# ADR 0010: Voice processing, media streaming and computer-audio sharing

**Status:** Accepted (Phase 7)

**Context:** Phase 7 covers three requirements from the [project brief](../../project-brief.md):

- **F-MEDIA-1:** play media into a channel, labelled as media rather than a microphone.
- **F-MEDIA-2:** an optional effects pipeline (EQ, filters, voice changer), kept out of the real-time path where it is not real-time safe.
- **F-SYS-1:** share computer audio with WASAPI loopback, per process where Windows supports it, without feedback.

Risk R5 in the [risk assessment](../../risk-assessment.md) adds a constraint: whole-device loopback also captures OpenTalk's own output, so it may only be offered with an explicit warning.

People also expect the basics of a voice program: noise suppression, automatic gain and echo cancellation for speakers.

## Decisions

### 1. Noise suppression, automatic gain and echo cancellation: speexdsp

**Options considered:**

| Option | For | Against |
|---|---|---|
| **speexdsp** (BSD-3-Clause) | Small C library. Packaged by vcpkg and every Linux distribution. Echo canceller (MDF) and preprocessor (noise suppression, AGC, residual echo suppression) designed to work together. Very low CPU cost. | Older algorithms; noise suppression is weaker on non-steady noise than neural methods. |
| WebRTC audio processing | Better echo cancellation and noise suppression. | Large build (abseil, many targets); no maintained vcpkg port; the standalone forks lag upstream. |
| RNNoise | Good neural noise suppression. | Noise only (no echo, no AGC); its model data needs its own licence check. |
| Windows' built-in communications processing | Free on Windows. | Not available through miniaudio's shared-mode WASAPI; behaviour differs per device driver; Windows only. |

**Decision:** speexdsp 1.2.1, behind OpenTalk's own `VoiceProcessor` and `EchoCanceller` classes (`src/audio/VoiceProcessing.*`), so WebRTC processing or RNNoise can replace it later without touching the engine.

- **Noise suppression is on by default.** It removes steady noise (fans, hum) and leaves speech within a few dB. It also removes steady tones, so the tone-based tests turn it off.
- **Automatic gain and echo cancellation are off by default.** Echo cancellation is for speakers; with headphones it only costs CPU.
- **Echo reference.** The echo canceller is given exactly what OpenTalk sent to the speakers, frame by frame. A queue matches each captured frame with the frame played at the same point. The queue starts again from the present when capture stalls or its clock drifts, because a reference older than the echo cannot cancel anything.
- **Limitation:** it removes only OpenTalk's own output. A screen reader speaking through the speakers is another program, so its speech is not removed. Headphones or push-to-talk are the advice for screen reader users.

### 2. Effects: OpenTalk's own small filters, on the audio worker thread

The effects are:

- a rumble filter (100 Hz high-pass)
- bass and treble shelves (±12 dB)
- a voice changer (pitch shift, ±12 semitones)

They are second-order filters from the Audio EQ Cookbook, plus a two-tap delay-line pitch shifter: about 150 lines of code with no dependency.

They run on the engine's worker thread after noise suppression, **never in the device callback**, which only copies samples. That meets F-MEDIA-2's "outside the real-time path" without a separate effects thread. In-process third-party effect plugins remain out of scope (ADR 0006: third-party code runs out of process).

### 3. Media: a built-in player, not a plugin

The roadmap said "media player plugin". Plugins talk JSON lines over standard input and output, limited to 20 messages per second ([ADR 0006](0006-plugin-isolation.md)). Audio needs 50 frames per second with steady timing, so a plugin cannot carry it.

Instead:

- **The client has a built-in player** (Audio menu). It streams a WAV, FLAC or MP3 file at normal speed as `StreamType::Media`, encoded as Opus "music" at 64 kbit/s by default, capped by the server's limit. The file is decoded a little at a time (`MediaFile`, built on miniaudio's decoders), so long files are fine. The sender hears it too, unless they turn that off.
- **Bots** get the same reader in the SDK (`AudioFileReader`, SDK 0.4) and send with `AudioStream::Media`. The example bot has `!play`.
- **Formats:** only those miniaudio decodes itself: WAV, FLAC and MP3. FFmpeg (for AAC, Ogg Vorbis, Opus files and video soundtracks) was rejected for now. Its licensing depends on the build options, and some of its codecs raise patent questions.

### 4. Computer audio: process loopback first, echo cancellation as the fallback

- **Windows 11 and Windows Server 2022 (build 20348 or later):** WASAPI process loopback in "exclude" mode captures everything except OpenTalk's own process tree, so other people's voices never go back to them. miniaudio exposes it as `wasapi.loopbackProcessID` with `loopbackProcessExclude`.
- **Windows 10:** only whole-device loopback exists, and it contains OpenTalk's own output. OpenTalk still offers sharing, with three safeguards:
  1. **Cancellation.** A second echo canceller removes OpenTalk's own output from the capture, using the exact mix OpenTalk sent to the speakers as its reference.
  2. **An explicit warning.** When sharing starts, the history and the screen reader warn that some of other people's voices, or the screen reader, may still be heard.
  3. **Advice.** The warning suggests sending OpenTalk's sound to a different device, which takes it out of the shared device's capture entirely.
- **Who may stream.** Shared audio is sent as `StreamType::SystemAudio`. Both media and system audio need the `media.transmit` capability, which a channel can withhold without withholding voice. Guests do not have it by default.
- **A virtual audio driver** (a cable device) was **not** built. It needs a signed kernel-mode driver, an EV certificate and ongoing maintenance. The costs are in the [feasibility report](../../audio/virtual-driver-feasibility.md) for the owner to decide.

## Measured (synthetic devices and generated signals; no sound hardware)

The details are in [benchmarks](../../audio/benchmarks.md).

| Measurement | Result |
|---|---|
| Steady noise (−35 dBFS) | Lowered by 12 dB or more |
| Speech level after noise suppression | Within −6/+3 dB |
| Echo of played speech (40 ms delay) | Lowered by 15 dB or more |
| Echo while both people talk | About 13 dB lower in the canceller alone; the local voice is kept within 8 dB |
| OpenTalk's own sound in whole-device loopback | Measured 32–43 dB down in the engine tests |
| Media pacing | 1.5 s file → 72–76 frames at real speed |

The OpenTalk sound in the whole-device loopback test was a steady tone mixed into the loopback with no delay or level change. Real Windows mixing adds resampling and volume changes, so expect less, and **this is not yet measured on Windows**.

## Consequences

- New dependency: speexdsp (BSD-3-Clause). On Windows it comes from vcpkg, on Linux from the system package. It is recorded in the [third-party register](../../licensing/third-party-register.md).
- Receivers keep separate jitter buffers per sender and stream type, so a person can talk while their file plays. The channel list says "playing a file" or "sharing computer audio", never "talking", for those streams.
- Listeners can mute files and computer audio from everyone (Audio menu) without muting voices.
- **Still to verify by hand on Windows:** real devices, process loopback on Windows 11, and the Windows 10 fallback with real speakers. The steps are in the [NVDA test script](../../accessibility/nvda-test-script.md) and the [benchmarks](../../audio/benchmarks.md).
