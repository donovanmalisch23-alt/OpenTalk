# Audio benchmarks

This page records what has been **measured** about OpenTalk's audio path, how it was measured, and what still needs a person with real sound hardware. The tool is `opentalk-audio-bench`; it ships with OpenTalk and is built from `tools/audio-bench`.

```
opentalk-audio-bench all               codec, jitter, router and network tests
opentalk-audio-bench device-loopback   round trip through real sound hardware
```

## Test setup for the published numbers

- **Machine:** a cloud virtual machine with 4 logical CPUs (Intel Xeon, 2.1 GHz) running Ubuntu 24.04. It has **no sound hardware**.
- **Build:** Release (`-O2`), GCC 13, Opus 1.4 from the system.
- **Date:** 28 October 2026, OpenTalk 0.1.0 development build.

A desktop or laptop will usually be faster per core than this machine. Run the tool on your own computer to get your figures.

## Results

### Opus cost per 20 ms frame (one CPU core)

| Bit rate | Encode (µs) | Decode (µs) | Concealment (µs) | Share of one core for one stream |
|---|---|---|---|---|
| 16 kbit/s | 378 | 30 | 29 | 2.0 % |
| 32 kbit/s | 478 | 35 | 30 | 2.6 % |
| 64 kbit/s | 492 | 48 | 30 | 2.7 % |

**Test signal:** a synthetic speech-like signal (a voice-like fundamental with formants and syllables), at Opus complexity 10. A client encodes one stream and decodes one stream per person talking, so ten people talking at once cost about 0.4 % of a core for decoding.

### Jitter buffer and loss recovery (simulated network)

- **Simulation:** 60 seconds of speech per row through the real jitter buffer and Opus decoder.
- **Network delay:** 20 ms plus a uniformly random extra delay.
- **Loss:** "random" losses are independent; "bursts" means a lost packet is followed by another loss half of the time.
- **Added delay:** how long a packet waits in the buffer before it plays.

| Network | Added delay median / p95 (ms) | Played normally | Recovered by FEC | Concealed | Late (dropped) |
|---|---|---|---|---|---|
| Perfect | 27 / 27 | 100.0 % | 0.0 % | 0.1 % | 0.0 % |
| 10 ms jitter | 22 / 27 | 100.0 % | 0.0 % | 0.1 % | 0.0 % |
| 30 ms jitter | 32 / 46 | 100.0 % | 0.0 % | 0.1 % | 0.0 % |
| 60 ms jitter | 38 / 65 | 100.0 % | 0.0 % | 0.2 % | 0.0 % |
| 2 % random loss | 41 / 47 | 98.0 % | 1.9 % | 0.2 % | 0.0 % |
| 5 % random loss | 42 / 47 | 95.1 % | 4.6 % | 0.4 % | 0.0 % |
| 10 % random loss | 42 / 47 | 89.8 % | 9.2 % | 1.2 % | 0.0 % |
| 5 % loss in bursts | 57 / 67 | 91.8 % | 3.9 % | 3.7 % | 0.0 % |

**How to read this:**

- **Recovered by FEC:** lost packets rebuilt from the forward error correction in the next packet. They sound close to the original.
- **Concealed:** lost packets filled in by Opus with a plausible continuation. At these rates it is a brief smear, not a gap.
- The buffer keeps one extra frame (20 ms) when it sees unrepaired losses, so error correction can work. That is why the loss rows have about 20 ms more delay than the jitter-only rows.
- **0.1 % concealed on a perfect network** is the frame where each talk spurt starts.

**These numbers led to a fix.** The first version of the jitter buffer kept 3 frames and only adapted between talk spurts, so it added 47 ms even on a perfect network. It now:

- starts at 2 frames
- grows one frame when a packet arrives too late or losses need it
- shrinks every 2 seconds when the extra delay is not needed

Unit tests cover all three.

### Server cost to forward voice (one CPU core)

The server's work per packet: decrypt one incoming UDP packet, then encrypt one copy for each listener. Sending through the operating system is not included (it adds roughly 1 to 5 µs per datagram, depending on the system).

| Listeners in the channel | Time per incoming packet (µs) | Per copy sent (µs) | Speakers one core could carry |
|---|---|---|---|
| 1 | 2.4 | 2.4 | about 8,300 |
| 10 | 11.5 | 1.2 | about 1,700 |
| 50 | 50.9 | 1.0 | about 390 |

"Speakers" assumes each speaker sends 50 packets per second to that many listeners. Even a small server can carry far more simultaneous talkers than a conversation can use.

### Latency through a real server on this computer

- **Setup:** one client sends 80-byte packets every 20 ms to another client in the same channel, through a real server on the same computer (localhost). Each packet carries its send time.
- **What it includes:** encryption, the server's forwarding, and decryption.
- **What it does not include:** the jitter buffer and sound devices.

| Path | Median (ms) | 95th percentile (ms) | Maximum (ms) | Delivered |
|---|---|---|---|---|
| Encrypted UDP | 0.09 | 0.14 | 1.3 | 500 of 500 |
| TLS connection (fallback) | 0.21 | 0.33 | 2.4 | 500 of 500 |

**This measurement also found a problem,** now fixed: without the `TCP_NODELAY` socket option, the fallback path occasionally held packets for about 40 ms, because of Nagle's algorithm combined with delayed acknowledgements. Both client and server now set it.

## Mouth-to-ear latency budget (computed, not measured)

**Not measured.** The machine above has no sound hardware, so end-to-end latency through real microphones and speakers has **not** been measured yet. This is the budget from the design, for two people on the same local network with a good connection:

| Stage | Typical delay |
|---|---|
| Capture device buffer (miniaudio, 10 ms periods, WASAPI shared mode) | 10–20 ms |
| Collecting a 20 ms frame | 20 ms |
| Opus encoding | under 1 ms |
| Network (local network, measured above) | under 1 ms |
| Jitter buffer (measured above) | 22–27 ms |
| Mixed audio queued for the playback device | 20–40 ms |
| Playback device buffer | 10–20 ms |
| **Total** | **about 85–130 ms** |

Over the internet, add the network delay between the two people and the server (often 10–80 ms each way), plus any extra jitter buffering the connection needs.

## Measuring with real hardware (help wanted)

`device-loopback` plays short clicks and listens for them. It reports the round trip from handing sound to the operating system until it comes back from the microphone.

1. Connect the sound card's output to its input with a cable. Alternatively, put the microphone next to the speaker in a quiet room.
2. Run `opentalk-audio-bench device-loopback`. Add `--input "device name"` and `--output "device name"` to choose devices.
3. The tool reports each click and the median round trip.

Please share your results by opening an issue. Include:

- Windows version and sound hardware
- whether you used a cable
- the output of `opentalk-audio-bench all`

**Status:** the device test has been built and runs, but it has **not** been run on real hardware. On the benchmark machine it correctly reports that no sound device can be opened.
