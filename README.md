# Ember

**Local-first voice and text AI for Windows.** Original native **C++20 + Qt 6 Quick** desktop UI. No WebView2, browser wrapper, Android emulator, cloud account, or compulsory subscription.

This repository currently contains a **development foundation**. It compiles an application and provides real integration points for local engines, but this is **not yet a fully tested release or a real-time, full-duplex conversation system**. No models or vendor executables are redistributed. Follow [the setup guide](docs/SETUP.md), [offline distribution plan](docs/OFFLINE-DISTRIBUTION.md) and [release gates](docs/RELEASE-GATES.md).

## Design priorities

- Speech first: choose a real Windows microphone → local `whisper.cpp` transcription → local `llama.cpp` response → either offline `sherpa-onnx` Kokoro or a separate **localhost-only** OpenAI-compatible Qwen3-TTS/Chatterbox service → selected Windows speakers.
- Explicit complete local .emberpack backup includes chats and image Library
  with SHA-256 verification. Legacy .emberbackup text-only backups remain
  compatible. Neither is encrypted; Drive sync is not yet integrated.
- Native local image Library: add from file/clipboard or reattach a saved
  image; bounded metadata-free JPEG/PNG, deduplicated SHA-256. A vision-capable
  local GGUF plus explicitly configured mmproj is required for actual image
  understanding; text-only models never pretend they saw an image.
- Text chat: the same local LLM can be used without a microphone. Current session is held in memory, not silently uploaded. Long voice replies are segmented on natural punctuation instead of dropping text after an arbitrary 700-character cutoff.
- Efficient native UI: GPU-rendered Qt Quick, subtle animations, dark colors, original procedural voice stage, keyboard-controlled composer, named audio-device and female-speaker selectors, local last-stage timing readouts. No runtime web UI.
- Replaceable intelligence: independently choose the voice backend (fast CPU Kokoro or higher-end local TTS API), the speaker and the local reasoner. Benchmark each component; model weight blending is a separate research path, not an assumed feature.
- Paired private RU/EN ASR measurements: `tools/compare_asr_engines.py` benchmarks user-installed Whisper.cpp and Qwen3-ASR on identical local 16 kHz corpus segments and writes only numeric error/latency summaries, with no copied recordings or transcripts. Run it manually when models are installed; see [voice quality lab](docs/VOICE-QUALITY-LAB.md).
- Portable storage under `D:\Games\Ember` by default, including `data/ember.ini`, per-session cache, engines and GGUF/ONNX weights. The actual install location is user-selected.
- Permission boundaries for future local files, programs, Internet and GitHub/Drive automation. No universal implicit access is granted by a text prompt. All current AI HTTP traffic is direct loopback with system proxies and redirects disabled.

## Current behavior

The backend launches a local `llama-server` bound to `127.0.0.1:43821` with one GGUF model and a fixed `ember` API alias. It posts current-session messages to the local `/v1/chat/completions` endpoint, never a remote URL. Audio recording runs only on explicit command, with a quota instead of a forced 30-second cutoff. The current system is push-to-talk, with first-sentence TTS streaming when supported, not full-duplex. A compatible local TTS server, when selected, must already be running on `127.0.0.1:43822`; requests cannot be redirected to remote addresses. The caller must supply compatible engine binaries and licensed models; common stereo and 44.1/48 kHz microphones are converted to 16 kHz mono with a bounded resampler, while real hardware/device selection still needs acceptance testing. The built-in Kokoro path currently targets the **English v0.19 voice pack**. CLI Whisper transcription and Kokoro synthesis have bounded per-segment watchdogs (180 seconds and 120 seconds respectively); unconsumed native engine diagnostics are discarded in memory, not archived. For stereo capture devices with strongly opposite-polarity microphones, the resampler conservatively selects a usable channel instead of cancelling speech. These are resilience measures, not hardware-validated audio quality improvements. Russian speech output and multilingual voice quality are future acceptance items, not implied by the English model.

### Build

Requires MSVC x64, CMake 3.21+ and dynamically linked Qt 6.5+ with Quick, QuickControls2, Multimedia and Widgets.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
windeployqt --qmldir qml --release build\Release\Ember.exe
```

Copy the deployed executable and Qt DLLs into `D:\Games\Ember`; place external engine binaries under `engines` and separately downloaded models under `models` following [SETUP.md](docs/SETUP.md). Then open **Settings**, verify all paths, start the local brain, and test the selected voice.

## Development state and downloads

GitHub Actions compiles the native Qt app and **tests** a versioned Inno Setup installer with silent installation, real QML startup and uninstall. Developer build files stay inside CI and are **not uploaded as previews**, by request. The public [Releases](https://github.com/ProhorovIlja/Ember/releases) page is reserved for accepted final installers with checksums, detailed notes, tested model inventory and GitHub source archives. The separate `.github/workflows/stable-release.yml` enforces reviewed hardware/quality/security gates, matching SemVer and clean installer acceptance before publication.

References and licensing: [docs/RESEARCH.md](docs/RESEARCH.md). Technical architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The Ember app code is MIT-licensed; Qt, models, GGUF converters and engine binaries retain their own licenses.

### Local chat and transcription functionality under development

Ember now includes a private **SQLite text-only chat archive** at `data/ember-chats.sqlite`; each conversation has its own local ID and title, and previous chats can be reopened from the sidebar. The UI loads up to 300 recent messages per session while SQLite keeps the complete text archive. Audio recordings are transient and never inserted into chat history. The archive is currently **plaintext**, not encrypted; there is no automatic Google Drive synchronization. Deleting a selected conversation is explicit, and the installer intentionally does not erase the SQLite database on uninstall.

The Chat composer provides **hold-to-dictate** with offline Whisper recognition in English or Russian (auto-detect, English, Russian). Speech capture is disk-backed and resampled in bounded ~25-second jobs; there is no 30-second dictation time limit, but there is a safety limit of **1 GiB of temporary raw capture**. The transcript is inserted into the composer for review and is **not automatically sent**. If a later ASR chunk fails during a long dictation, already recognized segments are recovered into the editable composer with an explicit incomplete-transcription warning; voice requests are never automatically sent from incomplete recognition. Voice conversations, meanwhile, use natural **English output** even after Russian speech input; the application interface is English-only. Text mode works in English/Russian and includes Conversation, Story studio, Troubleshooting and Development profiles.

Local text generation now consumes OpenAI/llama.cpp `text/event-stream` responses incrementally, checks malformed or oversized events, updates the chat UI in bounded batches and preserves interrupted partial text. Model reasoning received as a separate `reasoning_content` field or as streamed `<think>…</think>` markup is excluded from displayed assistant replies, voice playback, and saved SQLite chat history. Split tag prefixes remain withheld until the stream determines whether they are real tags; the raw stream exists only in bounded memory for the duration of the reply. This is token streaming for **text**, not duplex audio or a measured first-phoneme improvement. The initial LLM context is configured for 8,192 tokens and generation for up to 2,048 tokens when writing long stories; performance depends on the chosen weights, quantization and actual RTX 4070 measurements.

The visual direction is **AMOLED black, restrained electric-blue accents, an original blue-flame emblem, and native Qt Quick**. Basic Windows/QML smoke tests do not constitute pixel-level visual QA on the target GPU or at multiple DPI settings.
