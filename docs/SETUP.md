# Windows portable setup — first voice milestone

## Native local Image Library and single-image vision

The new C++/Qt gallery normalizes user-selected local files or explicitly
pasted clipboard images into metadata-free, bounded JPEG/PNG. They are stored
under `data/library` named by SHA-256 of their encoded image bytes; importing
the same normalized image twice does not duplicate storage. Image files stay
on the user's PC and are never silently uploaded. Import/paste is refused in
RAM-only Private mode, because that would create persistent files.
The gallery shows a local thumbnail, permits explicit attach or delete
and never loads a remote image URL.

To make a real vision request, select a compatible **local vision-capable
GGUF** as the brain model and choose its local `mmproj.gguf` in the
`Vision projector · optional` Settings field, then restart the brain.
The local llama.cpp server starts with `--mmproj`. Attach a single Library
image, then Send. Ember constructs the current-user OpenAI-style typed
`image_url` as a private JPEG/PNG data URI and sends it **only to**
`127.0.0.1:43821`, never to an external server. Without a configured
projector Ember refuses to analyze an image; it does not falsely claim to
see one. The text archive saves a SHA-256 reference, not base64 blobs.
A new explicit **Backup chats + images** action creates a full local
`.emberpack` (format `EMBER-COMPLETE-BACKUP-2`; fixed-length identifiers
to avoid allocating from untrusted length fields): the existing compressed chat archive plus SHA-256 checked
normalized Library images (JPEG/PNG already compressed on disk).
**Restore full** verifies the entire package before installing assets and
recreates chat sessions under new UUIDs without replacing saved chats.
The old `.emberbackup` text-only archive and Restore remain supported.
The new format is NOT encrypted and is not automatically synced to Drive;
only export it to storage you trust. A single package limits chats to 256 MiB
and images to 4096/1 GiB; larger libraries fail instead of silently skipping.
Filesystem and database are not one crash-atomic transaction: interrupted
restores may require recovery from the retained original archive.
Deleting an image leaves a visible missing-asset reference in old chats.
Model/mmproj compatibility and visual quality need actual RTX4070 validation.



**Offline native Qwen speech loading:** with downloaded packages blocked by
default, place a trusted GGUF at `models/speech/qwen3-tts-0.6b.gguf`,
`models/speech/qwen3-tts-1.7b.gguf`,
`models/speech/qwen3-asr-0.6b.gguf` or
`models/speech/qwen3-asr-1.7b.gguf` (only the selected model(s)).
The local GGUF is opened by explicit absolute file path, rather than a catalog
alias that triggers speech.cpp's `curl`-based model fetch.
The Settings toggle **Allow model downloads** is disabled by default;
allowing it is an explicit choice. The UI checks only the first four GGUF
magic bytes and file path safety, **not weight integrity or license**; use
the separate SHA-256 offline payload inventory before redistribution.
This does not guarantee a third-party executable never contacts the Internet;
a real disconnected-machine acceptance test remains mandatory.

**Offline packaging:** Ember's desired final distribution must install and run
fully without a VPN, zapret, GitHub, Hugging Face or model downloads. At this
development checkpoint, models and engine DLLs are NOT bundled. The new
`tools/offline_bundle.py` creates a per-file SHA-256 inventory, rejects
unlisted and symlinked files, and stages ONLY assets whose redistribution
approval and license-evidence files have been reviewed. See
[Offline distribution design](OFFLINE-DISTRIBUTION.md). No part of this
tool downloads or executes a model. An unaudited local import remains possible
using the native Settings Browse controls; public redistribution is different.



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

The Chat sidebar also provides **Backup all** and **Restore**. Backups use an opt-in local `.emberbackup` format with individually zlib-compressed JSON frames for every saved chat, message, story-continuity note and per-chat recognition vocabulary. Restore validates the stream with bounded decompression and an all-or-nothing SQLite transaction, importing chats as new copies without overwriting existing sessions. A truncated or corrupt file leaves the database unchanged. This format is **not encrypted**; keep copies in a protected location. No automatic cloud upload occurs. Private-session RAM-only conversations are intentionally excluded and backups/restores are disabled during Private mode. This is a prototype backup format and should not replace an independent offline copy of `ember-chats.sqlite` until round-trip testing on the target Windows PC is complete.

The sidebar searches saved conversations locally with a 180 ms debounce. Use **Export selected chat** to save the entire selected conversation as UTF-8 Markdown, including messages older than the current 300-item UI window. The export is an explicit user action, not automatic Google Drive synchronization. SQLite uses WAL for crash recovery; long-term retention and secure encryption remain separate acceptance tasks.

For reproducible model selection, run `python tools/text_benchmark.py --samples 8 --output text-metrics.json` only with an explicitly started trusted local llama-server. It compares SSE time-to-first-token, full-response time and reported tokens/s (or clearly marked characters/s when token usage is absent) across fixed English storytelling, troubleshooting, Russian and instruction-adherence probes. No generated text is stored. This does not measure reasoning/naturalness or replace human evaluation.

The local context builder now selects up to **24 most recent complete exchanges** within a **12,000-character request heuristic** instead of a fixed eight. It does not yet offer embeddings, semantic recall, exact token budgeting or model-generated rolling story summaries. For unusually long Russian prose, the llama-server's 8,192-token context can still need a higher-memory profile.

The private `data\\ember-chats.sqlite` database stores only text and session metadata. It survives normal application restarts; it is not automatically deleted on uninstall. Audio capture, transcripts awaiting insertion and generated WAV files are kept in temporary session cache, not in SQLite. The text database is currently **not encrypted**; enable your Windows disk encryption or control access to that folder for additional local confidentiality. There is no Google Drive sync until an explicit opt-in module is implemented.

**Dictation:** In Chat, hold **Hold to dictate**, speak English or Russian, then release. The transcript is inserted in the composer for editing; it is not sent to the LLM unless you press **Send message**. For better accuracy select English or Russian in Settings instead of Auto-detect. Voice conversations retain English spoken output; text and dictation accept both languages. Long dictation writes only to a temporary raw file (1 GiB quota, no 30-second stop) and is transcribed offline in bounded sequential chunks. Adjacent chunks now overlap by 500 ms in dictation mode; the transcript merger removes exactly repeated 2–12-word prefixes with casefolded RU/EN matching, preserving genuine single-word repetition. This reduces the chance of boundary word loss but does not guarantee correct recognition of words at every boundary. Failed recognition removes the temporary raw/WAV/text files instead of leaving them in the cache until a normal exit.

### Known limitations

- The UI reports LLM time-to-first-token and microphone release to the Qt player's PlayingState. This is an approximate playback-start measurement, not a physical first-sound timestamp measured at the speakers or headphones.
- Chat bubbles now present Ember answers using native Qt RichText with a deliberately restricted Markdown subset: headings, bold/emphasis, inline/fenced code, bullets and quotes. Raw text and Markdown links/images remain plain escaped text. No Qt RichText external image/network/file retrieval is possible from model-provided markup, and the SQLite/export content remains the unchanged original Markdown. The renderer uses a lightweight read-only QML Text item without a WebView or heavy browser-based renderer.
- Capture reliability: native microphone disk writes are bounded to the 1 GiB private scratch quota inside the audio callback. Atomic counters report quota/write faults to the UI watchdog without concurrently polling the QFile from another thread. Partial/failed disk writes discard corrupt audio rather than submitting it to ASR. Actual failure-path behavior on physical Windows devices remains to be validated.
- Live mic meter and experimental endpointing: source samples continue to stream to the existing private temporary raw capture file. A bounded native QIODevice tap computes RMS/peak only for the on-screen mic level; no additional sound file, background upload or log. By default voice recording now uses a deliberate **Stop recording** action without the old forced 30-second cutoff, like long dictation (still bounded by the 1 GiB temporary scratch quota). Optional **Experimental auto-finish** checks for sustained measurable input followed by approximately 2.2 seconds of quiet; the energy heuristic is not a trained VAD, so quiet words/music/noise can confuse it. It is **disabled by default**, never used for dictation, and needs real microphone trials.
- Push-to-interrupt: when Ember is speaking, click **Interrupt & speak** or hold **Interrupt & dictate**. The previous local TTS/LLM stream is cancelled before opening the microphone. An active ASR transcription cannot be interrupted this way. This is half-duplex manual barge-in, not VAD, echo suppression or simultaneous listening and talking.
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

The native server stays resident for subsequent sentences, uses the local health endpoint, is stopped explicitly or at Ember exit, and requires **no browser**. It is **not** started on application launch. By default Ember requires local GGUF files for managed speech.cpp and passes them by path; missing files are refused without a fetch. Enable **Allow model downloads** explicitly to permit catalog downloads of absent files. The native engine itself is not an OS network sandbox; a disconnected Windows acceptance test is required. Inference requests are sent only to `127.0.0.1:43822`; the model download is an external network operation and may require internet access. Verify the engine release and the particular GGUF **model license** before use. You may instead start a compatible third-party local TTS server; that option remains supported without Ember managing its lifecycle. No weights or speech.exe are currently bundled.

### Speech playback improvements

Long speech responses now use **two alternating private WAV slots**. While Windows plays one synthesized sentence block, the next can be produced in the opposite slot. Prefetch capacity is bounded to one ready block; output is never overwritten while the media player owns it. WAVs are deleted on normal completion, cancellation and ordinary engine errors; abrupt termination, Windows sharing violations and actual audio-device unplugging still require real-machine acceptance. API playback still waits for each **complete** PCM16 WAV: early incremental PCM audio, true full-duplex, microphone barge-in while TTS is synthesizing and perfect continuity are **not** implemented.

Text-only Markdown/code/link formatting remains intact in the chat archive. For spoken replies, Ember strips headings, emphasis, inline link URLs and fenced code blocks, replacing code blocks with a short spoken explanation. An approximate Cyrillic-majority guard declines Russian-dominant **spoken** synthesis and keeps the original text available; it is not a full language identifier or an automatic translator.

### Streaming PCM runtime

When the native `speech.exe` model has reached **Ready**, Ember automatically selects direct HTTP PCM streaming rather than waiting for the complete WAV response. The server must implement speech.cpp's `Content-Type: audio/pcm` + `X-Sample-Rate` contract. Ember writes signed 16-bit, single-channel audio to `QAudioSink` on the selected output device, enforces an internal PCM queue limit, and makes no temporary WAV for that streaming request. Third-party TTS APIs remain on the existing validated PCM16 WAV route, unless Ember itself owns a ready speech.cpp process. Cancelling, changing audio outputs or losing the current speaker stops the stream without a background recording.

Streaming PCM does not yet imply full-duplex, microphone barge-in or guaranteed zero stutters. Model and sound-card performance are still unverified on the target PC.

## SSE-sentence first-voice 2026-11-21

To enable the lowest-latency route currently implemented, start the local `llama-server` and, in Settings, select the native speech API engine and explicitly start the managed `speech.cpp` process. Wait until **Ready**. Then an Ember Voice recording can trigger local Qwen3-TTS after the first completed English sentence arrives over the llama.cpp SSE stream, without waiting for the entire response. Each PCM segment is still bounded, and the response text is saved to the usual local SQLite conversation only when the LLM turn completes.

Audio remains English-only. EN/RU recognition and typed chat are unchanged. Code fences and `<think>` reasoning are never read aloud, and speakers can be changed only when the current task finishes. Inference remains on localhost; first-run model weight downloads, if any, happen only through the explicitly started native model process. The experience remains **push-to-talk, turn-based**, not always-on full-duplex. Other local WAV servers and Kokoro CLI keep their existing whole-response route.

## LOCAL VOICE BENCHMARK 2026-11-21

After starting the native speech.cpp model locally on 127.0.0.1:43822, run `python tools/voice_benchmark.py --self-test` (protocol test only, no GGUF needed) or `python tools/voice_benchmark.py --voice vivian --samples 8` (actual locally running TTS). Optionally add `--output timing.json` to store **numeric timing metadata only**. No generated audio or microphone input is recorded, and prompts are fixed harmless English text, not chat history. Compare `vivian` with `serena` and any other license-approved voice using the same local environment.

The native Settings **Early Sentence Speech** control defaults to enabled and persists in `data/ember.ini`. Disable it if the local 12 GB GPU experiences contention from running LLM and resident TTS simultaneously; managed PCM streaming remains in use after the full answer. The default remains the fastest available source-level flow, but there are no unmeasured claims about actual acoustic latency or voice quality.

## 21 November 2026 — bounded, compact local story/technical recall

For long collaborative stories and technical investigations, Ember now retrieves **up to three short, relevant older text excerpts from the active SQLite conversation only**. It uses a bounded lexical scan (up to 2,048 older records, first 1,800 characters each), excluding the latest 48 messages normally represented in short-term history. No embedding model, duplicated FTS database, internet call, voice recording or automatic Drive backup is involved. Matching is case-folded and supports both English and Russian story facts. The model receives clearly labeled *unverified historical references* as user-level data, never privileged system instructions. The actual current user message remains last; explicitly pinned 1,800-character notes still define important canonical facts.

This is limited lexical recollection, not perfect deep memory: facts may be missed if wordings differ, occur after the scanned range, or are buried inside long entries. Old assistant messages may be wrong. Benchmarks on thousands of genuine conversations and model retrieval quality remain outstanding.

Local SQLite applies secure_delete, WAL autocheckpoint at 256 pages and journal_size_limit=2 MiB after checkpoint to reduce retained deleted text and idle journal footprint. This is not end-to-end encryption, a guaranteed absolute WAL size cap, secure physical SSD erasure, or Google Drive synchronization. See SQLite official https://www.sqlite.org/pragma.html and https://www.sqlite.org/wal.html.

Submitting a new message after the previous text generation has ended now cancels any *still-playing managed PCM audio* and clears the old HTTP/audio device state instead of stopping only the WAV player.

## Optional native Qwen3-ASR (21 November 2026)

Default EN/RU transcription remains `whisper-cli`, with optional Silero VAD and 25-second chunk overlap. In English Settings choose **Offline recognition engine**: Whisper (default), Qwen3-ASR 0.6B or Qwen3-ASR 1.7B. If selecting a native ASR model, **stop** a running speech.cpp server before switching, then explicitly press **Start native engine**. Ember launches an existing trusted `speech.exe` with the selected TTS and/or ASR model(s) on `127.0.0.1:43822`; the first start may download a licensed external GGUF. The two Qwen3-ASR sizes documented by upstream are roughly 0.84 GB and 2.18 GB at Q8_0, **not bundled**. When the native model is Ready, hold and release dictation or make an ordinary voice turn: the existing 25-second private WAV chunks are submitted via standard multipart `POST /v1/audio/transcriptions` with `model`, `language=auto|en|ru`, `response_format=text`; no proxy, redirect or remote host is accepted. Audio is deleted when completed or canceled. On an unready server, Ember shows a setup error instead of silently downloading or switching engines.

This is a second **optional local ASR backend**, not proven better than Whisper. Its actual English/Russian word error rates, accented speech quality, VRAM occupancy with concurrent LLM/TTS and Windows latency remain release-blocking measurements. Source contract: https://github.com/nyosegawa/speech.cpp/blob/main/docs/server.md and https://github.com/nyosegawa/speech.cpp/blob/main/docs/models/qwen3-asr.md.

## 21 November — privacy recovery for abandoned voice scratch files

Current Ember versions store microphone samples and synthesized WAV only in a private session-specific `cache/session-XXXXXX` folder and normally remove it on shutdown. After a crash, future launches now scan only directories with an exact Ember session-name pattern and an Ember-created `.ember.lock` marker older than 15 minutes; `QLockFile` must confirm the previous owner is not running before removal. Active sessions and unmarked folders are left untouched. This is best-effort local cleanup **after restart**, not secure overwrite of SSD sectors or a guarantee that a disk backup retained no audio. Old versions without a lock marker require manual review. Raw PCM streaming still stays in bounded RAM only.

The managed speech.cpp PCM request explicitly sets `language=en` for the English-only speech policy. Generic compatible complete-WAV servers retain their old field set without the speech.cpp-only extension.

### Local-only speech recognition accuracy probe

When a user-selected 25-second English or Russian WAV and its **locally provided** UTF-8 reference text are available, start the matching native speech.cpp ASR model, then compare `python tools/asr_benchmark.py --file .\\example.wav --reference-file .\\example.txt --model qwen3-asr-0.6b --language auto --output .\\asr-metrics.json`. The benchmark uses only loopback `127.0.0.1:43822`, no redirect/proxy or remote host; JSON reports **numeric WER/CER and wall time only**, not the original audio or either transcript. Test the 1.7B model separately with identical clips and VRAM conditions. `--self-test` runs against an in-process mock server with synthetic silent PCM and verifies request shape without downloading weights; Windows CI runs this mock test on each development branch push. This is not real-model recognition or any proof of English/Russian superiority.

Starting a new microphone turn after a previous answer's text generation has completed now cancels any still-playing native PCM/TTS audio (not just the legacy WAV player). This limits echo/feedback and avoids mixing a previous speech stream with the new recorded turn. It is **not yet** a continuous, full-duplex barge-in system: if an LLM/TTS request is still marked busy, users must cancel it first.

### Native English delivery control and output headroom — 21 November 2026

The existing native `speech.cpp` Qwen3 CustomVoice option can now be switched between lightweight **0.6B** and expressive **1.7B** resident models via English Qt Quick presets, but models/weights are **not downloaded until the user explicitly starts the native service**. Only **1.7B** receives optional `instructions` on its streaming PCM requests: natural/model-default, warm conversational, soft/gentle, bright/curious, and controlled story narration. The 0.6B upstream expressly refuses delivery instructions; Ember does not send incompatible JSON fields to it or to generic WAV servers. Style is a repeatable locally persisted parameter, not a guarantee of perceived emotion. The payload still explicitly requests `language=en` and compatible mono PCM.

The local Windows output has a native 0–100% persisted volume slider, applied to both QAudioOutput WAV playback and the active QAudioSink streaming PCM path. The PCM drain check now uses a **3 ms** timing tolerance instead of an earlier **20 ms** near-tail allowance to reduce premature teardown near utterance ends; Qt IdleState is still required. This scheduling change is verified by deterministic unit cases but **must still be listened to** on physical output devices.

Grounded upstream source: https://github.com/nyosegawa/speech.cpp/blob/main/docs/models/qwen3-tts.md — 1.7B instructions support, 0.6B rejection, 1.21 GB and 2.29 GB Q8_0 weight files, and RTX 2080 Vulkan results (not extrapolated to RTX 4070). https://github.com/nyosegawa/speech.cpp/blob/main/docs/server.md — native JSON `instructions` and PCM streaming schema. The official upstream reference notes that 0.6B can include substantially more leading digital silence than 1.7B; listening onset cannot be inferred from time to first HTTP audio byte. No forced silence trimming was introduced, to avoid cutting soft phonemes or expressive pauses.

### 21 November — first significant PCM level measurement without audio retention

The standard-library-only `tools/voice_benchmark.py` now additionally records the *length of initially low-level PCM16 audio* until the first sample with magnitude at least 256/32768 (roughly -42 dBFS peak). This diagnostic is distinct from first HTTP PCM byte and from **physical first audible sound** on the selected Windows headphone. The streaming parser tracks 16-bit sample boundaries even when HTTP chunks end on odd bytes, and saves no waveforms. Completely silent test clips return null rather than a fabricated onset; mock CI exercises a 50-ms synthetic silence followed by a synthetic tone. The benchmark also supports `--model qwen3-tts-1.7b --delivery warm|soft|bright|story` and rejects style instructions for 0.6B. Repeated trials with the exact same test phrases and voice are still required before any quality claims.

Example: `python tools/voice_benchmark.py --model qwen3-tts-1.7b --voice serena --delivery warm --samples 12 --output native-serena-warm.json`. The JSON contains numeric timing, seed-independent model/voice/style names and text-free metadata only, not PCM, recognition or user conversation.

### Per-conversation RU/EN recognition vocabulary (21 November 2026)

Optional native Qwen3-ASR now has a **per-conversation** text vocabulary for proper nouns, character names and technical words. The English Settings panel appears only for Qwen3-ASR, accepts up to 512 characters and saves them in a small `session_asr_hints` SQLite table keyed by the active session ID. When that specific conversation uses native recognition, at most 400 compact characters are added as the standard `prompt` field of the loopback-only `/v1/audio/transcriptions` multipart request; nothing is sent to external servers. The data is cleared from the appropriate table on session deletion and does not leak across chats. Whisper CLI is unchanged and never receives these native-only hints. The upstream Qwen3-ASR guide describes this prompt as background terminology, *not instructions*, but accuracy and hallucination risks still need genuine RU/EN listening tests.

Example hint for a story or development chat: `Mara, Yharnam, RTX 4070, C++, Qwen3, Анна`. It is user-maintained and optional; Ember does **not** silently extract identifiers from private Google Drive files or other chats.

### Native RAM-only private conversation mode — 21 November 2026

The English sidebar now offers **Start private session / Private session END**. Entering private mode stops the current reply/audio, starts a fresh conversation held only in application RAM, and never creates a SQLite session for it. The user's and assistant's text/continuity stays in the active process, and the mode has no selected persistent session ID. Exiting discards ephemeral messages and restores a previously saved chat; selecting any saved chat also exits private mode. Clearing the current private conversation does not affect saved chats. Export and persistent continuity/ASR vocabulary settings cannot write private content. The mode is **not** persisted in application settings.

Privacy limits are explicit: the local model still processes text in RAM, and a voice turn still creates temporary microphone raw/WAV files for local recognition which are removed after completion/normal shutdown (with best-effort abandoned scratch cleanup after crash). OS swap, hibernation, GPU process memory and forensic traces cannot be ruled out. The feature is **not encrypted memory, secure physical disk erasure, or a guarantee that all traces disappear**. It is an original native Ember implementation, not a clone of Sesame's undocumented app internals.


## Interim text-first local chat (manual offline install)

Before final voice acceptance, the native chat can run with only an installed
Qt app, a licensed compatible Windows llama-server and local GGUF weights.
See [Text-first milestone](TEXT-FIRST-MILESTONE.md) and the offline
`prepare-text-first.ps1` helper. The helper uses built-in PowerShell,
copies only local already-downloaded files with SHA-256 validation,
refuses replacements and never downloads or starts models. The code and
CI smoke tests do **not** guarantee a real model works until the target
Windows 10 machine runs its GGUF and completes full chat acceptance.


## Optional Deep reasoning for local text

The Conversation toolbar's `Deep · On` explicitly sets the request-local
`chat_template_kwargs.enable_thinking=true` and 4,096 maximum new tokens.
This is intended for a compatible Qwen3/llama.cpp GGUF; other model
templates or older llama.cpp builds may reject or ignore the flag. Use
`Deep · Off` if a local server errors, and start with actual Windows
GGUF tests before assuming a specific model works. Unlike ChatGPT's
cloud High setting, this simply spends local inference time and model
tokens; the existing 8,192-token context budget must still accommodate
the whole prompt and reply. There is no guaranteed equivalence to GPT-6 High.
