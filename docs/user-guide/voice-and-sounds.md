# Voice and sounds

This page explains how to talk and listen in OpenTalk, and how to change its sounds. Everything here works with the keyboard and a screen reader.

## Talking

Everyone in your channel hears you, and you hear everyone in your channel. Nobody in other channels hears you.

OpenTalk has three ways to decide when you are talking (Settings, Audio, "How to talk"):

| Mode | How it works | Good for |
|---|---|---|
| **Push-to-talk** (default) | You talk while you hold your push-to-talk key. | Most people, noisy rooms, and anyone who uses a screen reader without headphones. |
| **Voice activation** | You talk whenever the microphone hears you above a level you choose. | Hands-free use with a good headset. |
| **Always on** | Your microphone is always sent. | Quiet rooms with a headset; small groups. |

**Choosing a push-to-talk key:**

1. Press F4 and go to the Audio page (Ctrl+Tab moves between pages).
2. Press "Change key…".
3. Press and release the key or combination you want, for example F12 or Control+Windows. Escape cancels.

On Windows the key works even while another program is in front. The key still reaches that program too, so choose one it does not use.

**No key yet?** Press **Ctrl+T** in OpenTalk to start talking and Ctrl+T again to stop. This works in any mode.

**Shortcuts (Audio menu):**

| Keys | Does |
|---|---|
| Ctrl+T | Talk on/off without holding a key |
| Ctrl+M | Mute or unmute your microphone |
| Ctrl+Shift+M | Mute or unmute everything you hear (voices and sounds) |
| Ctrl+Shift+V | Volume for the selected person |
| Ctrl+Shift+F | Play a file into the channel |
| Ctrl+Shift+K | Pause or resume the file |
| Ctrl+Shift+J / Ctrl+Shift+L | Back / forward 10 seconds |
| Ctrl+Shift+X | Stop playing the file |
| Ctrl+Shift+I | Say what is playing, and where |
| Ctrl+Shift+C | Share your computer's sound on/off |

Each of these is spoken when you press it, for example "Microphone muted". The status bar's voice field shows "Talking", "Microphone muted", "Sound muted", "Streaming *file*" or "Sharing computer audio".

## Hearing others

- **Who is talking:** a person who is talking is shown in **bold** in the channel list, and their entry gets the description "talking". Their name itself does not change, so your screen reader does not re-read the line every time they speak. To hear "name talking" spoken, tick "Announce who starts talking" in Settings, Accessibility.
- **Files and computer sound:** when someone plays a file or shares their computer's sound, their entry says "playing a file" or "sharing computer audio" instead of "talking", so you always know it is not their microphone. The same setting announces it.
- **Muting files and computer sound from others:** Audio menu, "Mute files and computer audio from others". Voices are not affected. Settings, Voice Effects and Media, has a separate switch for each.
- **One person too loud or too quiet:** select them in the channel list or the Users list and press Ctrl+Shift+V. 100 % is unchanged, 0 % silences them, and up to 400 % makes them louder. The change is heard immediately and remembered for that person on that server. It only affects what you hear.
- **Everyone too loud or too quiet:** Settings, Audio, "Voice volume".

## Clearer voice

Settings, Audio, has three switches that clean up what you send:

- **Reduce background noise** (on by default) lowers steady sounds such as fans and hum. Turn it off to send music or an instrument unchanged.
- **Even out my voice level automatically** makes you louder when you speak quietly or sit far from the microphone.
- **Remove echo when using speakers** stops others hearing themselves when your microphone picks up your speakers. It cannot remove your screen reader's speech, because that comes from another program; for that, use headphones or push-to-talk.

## Voice effects

Settings, Voice Effects and Media, changes how your voice sounds to others. Everything is off unless you choose it:

- **Remove rumble** cuts low hum and knocks on the desk.
- **Bass** and **Treble**, from -12 to +12 dB. 0 leaves your voice unchanged.
- **Voice pitch**, from -12 to +12 semitones: a voice changer. 12 is a whole octave higher; -12 an octave lower. It adds a little delay.

## Playing a file into the channel

You can play a WAV, FLAC or MP3 file to everyone in your channel, for example music or a recording.

1. Press **Ctrl+Shift+F** (Audio menu, "Stream a file into the channel") and choose the file.
2. OpenTalk says "Streaming *name*, *length* long". You hear it too, unless you turn off "Hear files I stream" in Settings, Voice Effects and Media.
3. **Ctrl+Shift+K** pauses and resumes, **Ctrl+Shift+J** and **Ctrl+Shift+L** go back and forward 10 seconds, **Ctrl+Shift+I** says where it is, and **Ctrl+Shift+X** stops.

Your microphone keeps working while the file plays, so you can talk over it. The history says when the file ends. Streaming stops if you disconnect.

Others hear it labelled as a file, not as you talking. Settings, Voice Effects and Media, sets its volume and quality (64 kbit/s by default; the server may set a lower limit).

**Who may do it:** registered users can by default; guests cannot. An administrator can allow or forbid it per channel or per role (the "Stream media files and computer audio" permission). If nobody hears your file, the channel probably does not allow it.

## Sharing your computer's sound

**Ctrl+Shift+C** (Audio menu, "Share computer audio") sends everything your computer plays to your channel: a video, a game, a music player. Press it again to stop. It is available on Windows only.

- **On Windows 11** OpenTalk leaves its own sound out, so other people never hear themselves back.
- **On Windows 10** Windows cannot leave OpenTalk out. OpenTalk removes its own sound itself, but some of other people's voices, or your screen reader, may still come through; OpenTalk warns you when sharing starts. To avoid this completely, choose a different device for OpenTalk in Settings, Audio, "Speakers or headphones" (for example a headset), and play what you want to share on your main speakers.

The same permission as for files applies.

## Devices

Choose your microphone and your speakers or headphones in Settings, Audio. "System default" follows the device Windows uses.

If a device is unplugged, OpenTalk keeps trying every few seconds and continues when it is back. The history tells you about the problem once, and again when it is solved.

**Setting the voice activation level:**

1. Open Settings, Audio, and speak normally.
2. Read "Microphone level: … dB" under the level setting. NVDA users can read it with the review cursor.
3. Choose a level a little below your speaking level and above the background noise. A lower number (for example -55) is more sensitive.

## Connection quality

Voice normally uses a fast encrypted UDP connection. Some networks block UDP. Voice then continues over the main connection with a little more delay, and the history says so.

If voice keeps breaking up, try turning off "Use the faster UDP connection" in Settings, Audio. It applies from the next connection.

Lower "Voice quality" helps on slow connections. 32 kbit/s is recommended for speech; higher settings suit music.

## Event sounds

OpenTalk plays short sounds when:

- you connect or disconnect
- people join or leave the server or your channel
- messages arrive
- an error happens

Every event is also written to the history, so nothing depends on sound alone. Settings, Sounds turns sounds on or off, sets their volume, and controls the click when talking starts and stops.

The built-in sounds are generated by OpenTalk itself.

## Soundpacks

A soundpack is a folder of sound files that replace the built-in sounds.

1. In Settings, Sounds, press "Open soundpacks folder". This is `soundpacks` in your OpenTalk data folder.
2. Create a folder with your pack's name and put WAV, FLAC or MP3 files in it, named as in the table below.
3. Choose the pack in Settings, Sounds.

Missing files use the built-in sound. Each file can be up to 10 seconds long. Any sample rate works; OpenTalk converts to 48 kHz mono.

| File name (plus .wav, .flac or .mp3) | Played when |
|---|---|
| `connected` | You are connected and logged in |
| `disconnected` | The connection ends or is lost |
| `user-joined-server` | Someone connects to the server |
| `user-left-server` | Someone disconnects |
| `user-joined-channel` | Someone enters your channel |
| `user-left-channel` | Someone leaves your channel |
| `channel-message` | A message arrives in your channel |
| `private-message` | A private message arrives |
| `server-message` | An administrator sends a message to everyone |
| `transmit-on` | You start talking (push-to-talk or Ctrl+T) |
| `transmit-off` | You stop talking |
| `error` | Something went wrong (the history explains what) |

Packs placed in a `soundpacks` folder next to `OpenTalk.exe` are also listed. Only use sounds you have the right to use; OpenTalk does not include TeamTalk's sounds.
