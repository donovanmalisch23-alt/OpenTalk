# Voice transport

This page specifies how voice travels between clients and the server. The control protocol is in [proposal.md](proposal.md); the design decision is [ADR 0008](../architecture/decisions/0008-voice-encryption.md).

## Overview

1. After a successful login (`AuthResult` and `StateSnapshot`), the server sends **AudioSetup**. It contains:
   - the UDP port (the same number as the TLS port; 0 if the server offers no UDP)
   - a random 32-bit UDP session ID
   - two fresh random 32-byte keys, one per direction
   - the highest bit rate the server forwards
2. The client opens a UDP socket and sends encrypted **pings** to the server, every second until the first answer, then every 5 seconds.
3. While pongs keep arriving (within the last 12 seconds), the client sends voice over UDP. Otherwise it sends **AudioData** messages over the TLS connection (the *tunnel*).
4. Each ping tells the server whether the client is receiving its UDP packets. The server sends a client voice over UDP only if:
   - the last ping said so, and
   - the client was heard from over UDP in the last 20 seconds.

   Otherwise it uses the tunnel. A client that sends audio through the tunnel is also switched to the tunnel for receiving, until its next ping.
5. The server forwards each packet, without decoding it, to the other users in the speaker's channel. It replaces the user ID field with the speaker's real ID.

Voice therefore always works when the TLS connection works; UDP only makes it faster.

## Audio format

- **Codec:** Opus, 48 kHz, mono, 20 ms per packet, with in-band forward error correction.
- **Packet size:** 1 to 1275 bytes of Opus data.
- **Streams:** one sequence-numbered stream per user and stream type: 0 voice, 1 media file, 2 shared computer audio. Sequence numbers are 16-bit and wrap around. A user may send several types at once (for example talking over a file).
- **Labelling:** clients must present types 1 and 2 as "playing a file" and "sharing computer audio", never as the person talking, so listeners know it is not their microphone. Media and computer audio are encoded as Opus music (the desktop app defaults to 64 kbit/s).
- **Bit rate:** clients cap their encoders to the bit rate in AudioSetup (default 128 kbit/s).

## UDP datagram

```
offset size  field
0      1     kind         1 = audio, 2 = ping, 3 = pong
1      4     sessionId    udpSessionId from AudioSetup, big-endian
5      8     counter      per sender and direction, starts at 1, big-endian
13     n+16  ciphertext   XChaCha20-Poly1305(plaintext), 16-byte tag at the end
```

- **Key:** `clientToServerKey` for datagrams from the client; `serverToClientKey` for datagrams from the server.
- **Nonce (24 bytes):** the counter (8 bytes, big-endian), then the direction (0 = client to server, 1 = server to client), then 15 zero bytes.
- **Associated data:** the 13 header bytes.

The receiver rejects a datagram whose session ID is unknown, whose authentication fails, or whose counter it has already accepted or is more than 63 behind the highest counter it has accepted. Rejected datagrams are dropped silently.

### Plaintext by kind

| Kind | Layout |
|---|---|
| Audio | `u8 stream`, `u16 sequence`, `u32 userId` (0 from clients; the speaker from the server), then the Opus packet |
| Ping (client only) | `u64 token`, `u8 flags` (bit 0 = "I am receiving your UDP packets") |
| Pong (server only) | `u64 token` copied from the ping |

All integers are big-endian. A datagram is at most 13 + 7 + 1275 + 16 = 1311 bytes, which fits in common path MTUs without fragmentation.

## Control messages

| Type | Name | Direction | Fields (CBOR map keys) |
|---|---|---|---|
| 0x0400 | AudioSetup | S→C | 1 udpPort (uint16), 2 udpSessionId (uint32), 3 clientToServerKey (bytes, 32), 4 serverToClientKey (bytes, 32), 5 maxBitrate (uint32) |
| 0x0401 | AudioData | both | 1 stream (0–2), 2 sequence (uint16), 3 payload (bytes, 1–1275), 4 userId (uint32; omitted or 0 from clients) |

AudioData frames have their own rate limit (sustained 200 per second, burst 400). Excess frames are dropped rather than ending the session, so a burst of voice never disconnects anyone.

## Limits and abuse handling

- **Per-session audio budget:** 200 packets per second sustained, burst 400, across UDP and tunnel. This covers three simultaneous 20 ms streams with margin; excess is dropped and counted.
- **Slow receivers:** a client whose TLS connection has more than 256 KiB waiting to be sent gets no tunnelled audio until it catches up, so it cannot build up delay or server memory.
- **Unauthenticated datagrams** cost the server one hash lookup and, at most, one failed decryption.
- **Who hears what:** recipients are decided by the server: the other users in the sender's channel, if the sender may transmit there and is not voice-muted. Voice (type 0) needs the `voice.transmit` capability; media and computer audio (types 1 and 2) need `media.transmit`, which a channel or role can withhold separately. Audio that is not allowed is dropped silently.

## Client playback (informative)

- **Jitter buffer:** each incoming stream has its own adaptive buffer. It starts with 2 packets of delay and adapts between 2 and 10 packets based on measured jitter.
- **Lost packets:** recovered from the next packet's forward error correction when it has arrived, otherwise concealed by Opus.
- **End of a stream:** detected after 3 consecutive missing packets. The talking indicator turns off and the stream's buffers are released after 30 seconds of silence.
- **Listener choices:** the desktop app can mute types 1 and 2 from everyone without muting voice.
