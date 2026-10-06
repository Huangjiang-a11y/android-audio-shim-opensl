![Build](https://github.com/Huangjiang-a11y/android-audio-shim-opensl/actions/workflows/build.yml/badge.svg)

# Android Audio Shim — OpenSL ES fork

Enables [Simple Voice Chat](https://modrinth.com/plugin/simple-voice-chat) (and any
`javax.sound.sampled` consumer) to capture and play audio on Android, by implementing the
Java Sound SPI on top of **native Android audio APIs**.

This is a fork of **[Hhhrty2/android-audio-shim](https://github.com/Hhhrty2/android-audio-shim)**
with one substantial change:

> **The microphone path now uses OpenSL ES too.** Upstream used *AAudio* for input and
> *OpenSL ES* for output. Here **both capture and playback go through OpenSL ES**, so the
> whole shim depends on a single, universally available audio API.

---

## Features

- Java Sound SPI implementation (`javax.sound.sampled.spi.MixerProvider`)
- **Input:** OpenSL ES `AudioRecorder` (`SL_IID_RECORD` + `SL_IID_ANDROIDSIMPLEBUFFERQUEUE`)
- **Output:** OpenSL ES `AudioPlayer`
- Mono / stereo, 16-bit PCM, little endian
- Handle-based, multi-stream capable (up to 4 inputs, 16 outputs)
- Native library bundled in the JAR and extracted automatically on launch
- Fine-grained negative error codes for troubleshooting

## Requirements

| | |
|---|---|
| Platform | arm64 (aarch64) |
| Android | 8.0+ (API 26+ recommended; OpenSL ES itself works much lower) |
| Launchers | MojoLauncher, PojavLauncher, FCL — anything exposing the Java Sound API |
| Minecraft | 1.12.2+ (Fabric or Forge) |

Permissions: `RECORD_AUDIO` must be granted to the launcher.

---

## Building

Requirements: **JDK 8+** and **Android NDK r25+**.

```bash
ANDROID_NDK_HOME=/path/to/android-ndk ./build.sh
```

Outputs, in the repository root:

```
audio-shim.jar       # drop this into your mods folder
libaudioshim.so      # also bundled inside the jar
```

Optional overrides:

```bash
API=24 ./build.sh                  # target a different API level
```

---

## Installation

1. Copy `audio-shim.jar` into your Minecraft `mods/` folder.
2. In `voicechat-client.properties`, set:

   ```properties
   use_natives=false
   ```

3. Make sure the launcher has the `RECORD_AUDIO` permission.

> The mod does not appear in the mod list — this is expected.
> Do **not** place two copies of the shim (e.g. upstream + this fork) in `mods/` at once.

---

## What changed vs. upstream

### 1. Input migrated from AAudio to OpenSL ES

`NativeAudio` was rewritten to be **handle-based** (matching `NativeAudioOutput`), and its
JNI implementation was replaced with an OpenSL ES `AudioRecorder`.

Because raw AAudio access is gone, the library now links only against `libOpenSLES.so`
and `libdl.so` — no `libaaudio.so` dependency at all.

### 2. Correct recorder source/sink wiring

OpenSL ES recoding data flows **mic (IODevice) → buffer queue**. The IODevice is therefore
the *source* and must carry **no** format; the buffer queue is the *sink* and carries the
PCM format:

```c
SLDataSource source = { &locator, NULL };     /* mic device, no format   */
SLDataSink   sink   = { &bqLoc,   &pcmFmt };  /* buffer queue + format   */
```

(Getting this backwards makes `CreateAudioRecorder` fail outright.)

### 3. `available()` now reports the truth

`AndroidTargetDataLine.available()` used to return a constant buffer size, which is always
`> 0` and breaks any `while (available() > 0) read(...)` loop. It now queries the native
layer for the real readable byte count.

### 4. `read()` can actually end

The blocking `read()` had no way out: once the buffer was drained it would wait forever,
so "recording stopped" never fired. Now:

- `stop()` / `close()` set a `stopping` flag and `pthread_cond_broadcast()`
- a blocked `read()` wakes up and returns `0` (end of stream)
- `start()` resets the flag, so `stop()` → `start()` can be reused

### 5. Cached `.so` can no longer shadow a new build

The extraction logic used to skip writing when the target file already existed, so a stale
cached library would keep being loaded. It now always attempts to write, and refuses to
load a mismatched cached file it cannot overwrite.

### 6. Fine-grained error codes

| Code | Meaning |
|------|---------|
| `-400` | no free input slot |
| `-401/-402` | allocation failure |
| `-403/-404/-405` | engine create / realize / get-interface failure |
| `-40600X` | `CreateAudioRecorder` failed; `X` is the `SLresult` (`-406005` = unsupported content, e.g. sample rate) |
| `-407/-408/-409` | recorder realize / get buffer-queue / get record-interface failure |
| `-410` | callback registration failed |
| `-200`/`-201`/`-202`/`-300` | output slot / allocation / player setup failure |

---

## Layout

```
audio-shim-opensl/
├── build.sh                       # NDK-based build
├── native/audio_bridge.c          # JNI bridge (OpenSL ES in + OpenSL ES out)
├── src/de/maxhenkel/shim/
│   ├── AndroidMixer.java          # javax.sound Mixer
│   ├── AndroidMixerProvider.java  # SPI entry point
│   ├── AndroidSourceDataLine.java # speaker (OpenSL ES player)
│   ├── AndroidTargetDataLine.java # microphone (OpenSL ES recorder)
│   ├── NativeAudio.java           # input JNI + library loader
│   └── NativeAudioOutput.java     # output JNI
├── META-INF/services/javax.sound.sampled.spi.MixerProvider
├── resources/{fabric.mod.json,mcmod.info}
└── LICENSE
```

---

## Credits

- Upstream project: [Hhhrty2/android-audio-shim](https://github.com/Hhhrty2/android-audio-shim)
  (author credited as **GRED**)
- This fork: OpenSL ES input migration + the fixes listed above

## License

MIT — see [LICENSE](LICENSE). Original copyright (c) 2026 Hhhrty2.
