# Ember 0.1.0 — development foundation (NOT PUBLISHED)

- Original C++20 / Qt 6 Quick native dark interface: Voice, Chat and Settings.
- User-selected local llama.cpp GGUF runner (loopback 127.0.0.1 only).
- Explicit microphone recording (30 second cap), whisper.cpp speech recognition handoff and Kokoro/sherpa-onnx TTS command pipeline.
- Portable runtime settings stored beside the executable under `data`; bounded in-memory conversations and model limits.
- Testable C++ core with PCM WAV serialization, bounded RIFF/PCM16 validation, local-only request checks and safe sentence segmentation.
- Native Windows microphone/speaker selection stored by device identity; unplugged devices abort active capture and revert to Windows default, with a capture memory watchdog and per-session recording timer.
- Additional offline TTS provider: opt-in OpenAI-compatible Qwen3/Chatterbox speech API on fixed `127.0.0.1:43822` (no remote URLs, proxies or redirects); configurable local model alias and voice; response must be 16-bit PCM WAV.
- Readouts for last successful local speech recognition, LLM completion and TTS-chunk generation latency. These are **local timings, not quality scores**.
- Complete spoken replies are segmented on sentence boundaries instead of silently truncating after 700 characters; queued audio can be cancelled and its prior file handle is released before regenerating.
- User-editable, locally persisted comparison script (English or Russian with multilingual model), repeatable across selected voices; the Chat screen now includes an explicit **Stop reply** button.
- A reproducible Inno Setup **development** installer and portable ZIP compiled by CI; silent install, actual QML launch and uninstall smoke test; installer SHA-256 in artifact. A separate **fail-closed stable release workflow** validates hardware, voice quality, licenses, notes and reviewer evidence before any final Release.

Limitations: requires separate models and binaries; hardware and audio quality unverified; no real-time full-duplex calls or automatic installers. This document is a draft and must not be published as a stable download.

Further development after first dev installer: private SQLite text history with multiple named sessions, explicit delete, RU/EN press-and-hold disk-backed dictation, native English-only voice preference, text SSE streaming, conversation/story/troubleshoot/development profiles, custom AMOLED-black/electric-blue Qt Quick design and original flame emblem. The user explicitly declined previews: **development CI no longer uploads artifacts**. Only the final human/hardware accepted GitHub Release is distributable.
