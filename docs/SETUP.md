# Windows portable setup — first voice milestone

**Target computer:** Ryzen 5 5600 / NVIDIA RTX 4070 (12 GB VRAM) / 32 GB RAM / two SSDs. Preferred installation `D:\Games\Ember`. This path is *recommended*, not hardcoded: the application uses its executable folder for portable settings and private audio scratch data.

## Portable folder layout

```text
D:\Games\Ember\
    Ember.exe                 (Qt deployment)
    Qt6Core.dll, ...          (windeployqt result)
    data\ember.ini           (created on first config save)
    cache\                   (temporary session WAV files; deleted on clean exit)
    engines\
        llama\llama-server.exe
        whisper\whisper-cli.exe
        sherpa\sherpa-onnx-offline-tts.exe
    models\
        llm\model.gguf
        stt\ggml-base.bin
        stt\ggml-silero-v6.2.0.bin   (optional, for speech activity detection)
        tts\kokoro-en-v0_19\
            model.onnx
            voices.bin
            tokens.txt
            espeak-ng-data\       (phonemization resources)
```

1. Get **Windows x64/CUDA** `llama-server` from the upstream [llama.cpp](https://github.com/ggml-org/llama.cpp) releases or compile its CMake project with CUDA support. Preserve its accompanying DLL files next to the executable. Select a *verified GGUF conversion* of an open-license chat model; for a 12 GB RTX 4070, start benchmarking an approximately 8–9B-class model at Q4 rather than assuming all 16-bit weights will fit. There is no bundled or automatically downloaded model.
2. Get `whisper-cli` from [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (build with optional CUDA) and a multilingual GGML model (e.g. `ggml-base.bin` from upstream). The microphone first requests 16 kHz mono Int16. If that is unavailable, the app accepts the device's preferred UInt8/Int16/Int32/Float PCM format and uses a bounded windowed-sinc filter to resample it into 16 kHz mono PCM for Whisper.
   Optional: download the GGML Silero VAD v6.2 model from the official [whisper.cpp](https://github.com/ggml-org/whisper.cpp#voice-activity-detection-vad) instructions. In Settings, choose **Silero speech detection · optional**, or place it at `models/stt/ggml-silero-v6.2.0.bin`. When the readable model is present, Ember passes `--vad -vm <model>` to whisper-cli. This lets Whisper skip detected nonspeech spans within a recording. Older whisper-cli builds without VAD support should leave this file absent. Real RU/EN recognition/false-positive rates still need hardware measurements.
3. Get the offline TTS executable and an **English Kokoro** model pack from [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx). The app expects `model.onnx`, `voices.bin`, `tokens.txt`, and the **`espeak-ng-data` subfolder** from the original English **v0.19** package. The official v0.19 command uses `--kokoro-data-dir` and **does not require a lexicon file**. Retain the upstream licenses. The native selector names all eleven verified IDs (0–10): af, Bella, Nicole, Sarah, Sky, Adam, Michael, Emma, Isabella, George and Lewis. Settings include an **editable voice comparison script** (up to 1200 characters); save it and choose **Test voice** to hear identical content on each candidate. For multilingual output, a separate Kokoro pack and extra phoneme resources will require a reviewed adapter.
4. Build the Qt app with MSVC / Qt 6 or use a successful unsigned CI **development artifact**. Run `windeployqt --qmldir qml --release build\Release\Ember.exe`. Copy its deployed output to your preferred D-drive folder.
5. Open **Settings**, choose your **Microphone** and **Speakers** by name, and use the native **Browse** buttons for each selected model/engine. Device preferences persist by opaque system ID rather than list position; if hardware disappears, Ember stops the affected capture or playback and falls back to Windows defaults.
6. For a more expressive/multilingual **alternative to Kokoro**, install a separate licensed self-hosted speech server exposing `POST /v1/audio/speech` and configure it to listen on `127.0.0.1:43822`. Supported server families include [second-state/qwen3_audio_api](https://github.com/second-state/qwen3_audio_api), [Qwen3-TTS native server](https://github.com/gabriele-mastrapasqua/qwen3-tts), and [devnen/Chatterbox-TTS-Server](https://github.com/devnen/Chatterbox-TTS-Server). The protocol is optional: **Ember does not install, download or start these servers**. Select **Qwen3 / Chatterbox · local API**, set the server's actual model alias and speaker name (e.g. `qwen3-tts` + `Vivian` on a compatible Qwen server), then use **Test voice**. The endpoint is fixed to localhost port 43822. Ember expects a complete uncompressed 16-bit PCM WAV response (8 MiB maximum); services returning MP3/Opus/JSON or chunked streaming must be configured to return WAV. A failed/offline server shows an error rather than silently falling back to remote inference.
7. Start the brain and wait for local `/health`. Test typed chat, then speech synthesis, then **Record voice** → **Stop recording**. An RTX 4070 12 GB cannot be assumed to run a full-size LLM and multiple large GPU speech models simultaneously; first benchmark Kokoro on CPU with the GPU devoted to the LLM, then test Qwen 0.6B separately before enabling both.

Engine paths are explicit, not dynamically downloaded. Never copy random executable builds from unknown repositories. All model inference and voice synthesis occur locally if you use the upstream binaries and configuration described.

### Local development and final distribution

The CI runner assembles and installs a per-user Inno Setup EXE to confirm that the native Qt interface actually starts and can be removed. **No preview artifact is uploaded or advertised.** The final, acceptance-gated GitHub Release will contain the verified Windows EXE, portable ZIP, SHA-256 checksums and versioned release notes. The default folder suggestion remains `D:\\Games\\Ember` if drive D exists.

Each conversation now also has an editable **Memory** panel. Enter up to 1,800 characters of durable facts (characters, story rules, unfinished threads or recurring project details). These notes are stored in a separate SQLite table and loaded only for that conversation, so different chats never borrow each other's manually pinned facts. Notes enter the next system context as user-authored reference; they are not automatically verified, summarized, embedded or synced to Google Drive. They are included in explicit Markdown conversation export. This is a small story-continuity aid, not full long-term semantic memory.

The sidebar searches saved conversations locally with a 180 ms debounce. Use **Export selected chat** to save the entire selected conversation as UTF-8 Markdown, including messages older than the current 300-item UI window. The export is an explicit user action, not automatic Google Drive synchronization. SQLite uses WAL for crash recovery; long-term retention and secure encryption remain separate acceptance tasks.

The local context builder now selects up to **24 most recent complete exchanges** within a **12,000-character request heuristic** instead of a fixed eight. It does not yet offer embeddings, semantic recall, exact token budgeting or model-generated rolling story summaries. For unusually long Russian prose, the llama-server's 8,192-token context can still need a higher-memory profile.

The private `data\\ember-chats.sqlite` database stores only text and session metadata. It survives normal application restarts; it is not automatically deleted on uninstall. Audio capture, transcripts awaiting insertion and generated WAV files are kept in temporary session cache, not in SQLite. The text database is currently **not encrypted**; enable your Windows disk encryption or control access to that folder for additional local confidentiality. There is no Google Drive sync until an explicit opt-in module is implemented.

**Dictation:** In Chat, hold **Hold to dictate**, speak English or Russian, then release. The transcript is inserted in the composer for editing; it is not sent to the LLM unless you press **Send message**. For better accuracy select English or Russian in Settings instead of Auto-detect. Voice conversations retain English spoken output; text and dictation accept both languages. Long dictation writes only to a temporary raw file (1 GiB quota, no 30-second stop) and is transcribed offline in bounded sequential chunks. Adjacent chunks now overlap by 500 ms in dictation mode; the transcript merger removes exactly repeated 2–12-word prefixes with casefolded RU/EN matching, preserving genuine single-word repetition. This reduces the chance of boundary word loss but does not guarantee correct recognition of words at every boundary. Failed recognition removes the temporary raw/WAV/text files instead of leaving them in the cache until a normal exit.

### Known limitations

- The UI reports LLM time-to-first-token and microphone release to the Qt player's PlayingState. This is an approximate playback-start measurement, not a physical first-sound timestamp measured at the speakers or headphones.
- Text now streams over loopback SSE; audio is still not live full-duplex and each ASR/TTS chunk starts its own process except for the optional persistent TTS server.
- Optional Silero VAD preprocessing is wired for whisper-cli when VAD weights are configured. It is not a streaming VAD, echo cancellation or latency guarantee. The detector can miss unusually quiet speech; compare with VAD disabled if it does.
- Common 44.1/48 kHz mono/stereo microphones are resampled to 16 kHz with anti-alias filtering. Native input/output selection, persisted device IDs, capture memory watchdog and basic device-disconnection handling are implemented, but actual USB/Bluetooth fault testing, VAD, echo cancellation, barge-in and full-duplex remain pending.
- Spoken output is intentionally English-only. English/Russian speech **input** and typed text are supported via multilingual Whisper; Russian TTS is not a current release requirement.
- No model weight, audio or Windows packaging QA has been run on the user's own RTX 4070. A successful CI compile is not that test.
- No file/tool control, autonomous Internet/GitHub/Drive, multimodal vision or content generation is included in the first milestone.

## Native speech.cpp voice model (explicitly user-started)

An optional [speech.cpp](https://github.com/nyosegawa/speech.cpp) installation provides a native GGML/Vulkan-compatible Qwen3-TTS server. Place a **trusted Windows x64** `speech.exe` plus its required runtime files at `engines/speech/speech.exe`, or choose the executable under **Model routing** in Settings. Set **Synthesis engine** to **Qwen3 / Chatterbox · local API**, **Model alias** to `qwen3-tts-0.6b` and **Voice ID** to `vivian` (both names are case-sensitive for speech.cpp). Click **Start native TTS**. Ember starts the owned process with:

```text
speech.exe serve qwen3-tts-0.6b --host 127.0.0.1 --port 43822 --device auto --threads 4
```

The native server stays resident for subsequent sentences, uses the local health endpoint, is stopped explicitly or at Ember exit, and requires **no browser**. It is **not** started on application launch. On its first run speech.cpp may download GGUF model weights from Hugging Face. The Settings UI states this before the user starts the process. Inference requests are sent only to `127.0.0.1:43822`; the model download is an external network operation and may require internet access. Verify the engine release and the particular GGUF **model license** before use. You may instead start a compatible third-party local TTS server; that option remains supported without Ember managing its lifecycle. No weights or speech.exe are currently bundled.

### Speech playback improvements

Long speech responses now use **two alternating private WAV slots**. While Windows plays one synthesized sentence block, the next can be produced in the opposite slot. Prefetch capacity is bounded to one ready block; output is never overwritten while the media player owns it. WAVs are deleted on normal completion, cancellation and ordinary engine errors; abrupt termination, Windows sharing violations and actual audio-device unplugging still require real-machine acceptance. API playback still waits for each **complete** PCM16 WAV: early incremental PCM audio, true full-duplex, microphone barge-in while TTS is synthesizing and perfect continuity are **not** implemented.

Text-only Markdown/code/link formatting remains intact in the chat archive. For spoken replies, Ember strips headings, emphasis, inline link URLs and fenced code blocks, replacing code blocks with a short spoken explanation. An approximate Cyrillic-majority guard declines Russian-dominant **spoken** synthesis and keeps the original text available; it is not a full language identifier or an automatic translator.

### Streaming PCM runtime

When the native `speech.exe` model has reached **Ready**, Ember automatically selects direct HTTP PCM streaming rather than waiting for the complete WAV response. The server must implement speech.cpp's `Content-Type: audio/pcm` + `X-Sample-Rate` contract. Ember writes signed 16-bit, single-channel audio to `QAudioSink` on the selected output device, enforces an internal PCM queue limit, and makes no temporary WAV for that streaming request. Third-party TTS APIs remain on the existing validated PCM16 WAV route, unless Ember itself owns a ready speech.cpp process. Cancelling, changing audio outputs or losing the current speaker stops the stream without a background recording.

Streaming PCM does not yet imply full-duplex, microphone barge-in or guaranteed zero stutters. Model and sound-card performance are still unverified on the target PC.

## SSE-sentence first-voice 2026-11-21

To enable the lowest-latency route currently implemented, start the local `llama-server` and, in Settings, select the native speech API engine and explicitly start the managed `speech.cpp` process. Wait until **Ready**. Then an Ember Voice recording can trigger local Qwen3-TTS after the first completed English sentence arrives over the llama.cpp SSE stream, without waiting for the entire response. Each PCM segment is still bounded, and the response text is saved to the usual local SQLite conversation only when the LLM turn completes.

Audio remains English-only. EN/RU recognition and typed chat are unchanged. Code fences and `<think>` reasoning are never read aloud, and speakers can be changed only when the current task finishes. Inference remains on localhost; first-run model weight downloads, if any, happen only through the explicitly started native model process. The experience remains **push-to-talk, turn-based**, not always-on full-duplex. Other local WAV servers and Kokoro CLI keep their existing whole-response route.
