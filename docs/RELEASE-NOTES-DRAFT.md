# Ember 0.1.0 — development foundation (NOT PUBLISHED)

- Original C++20 / Qt 6 Quick native dark interface: Voice, Chat and Settings.
- User-selected local llama.cpp GGUF runner (loopback 127.0.0.1 only).
- Explicit microphone recording (30 second cap), whisper.cpp speech recognition handoff and Kokoro/sherpa-onnx TTS command pipeline.
- Portable runtime settings stored beside the executable under `data`; bounded in-memory conversations and model limits.
- Testable C++ core with PCM WAV serialization and local-only request checks.

Limitations: requires separate models and binaries; hardware and audio quality unverified; no real-time full-duplex calls or automatic installers. This document is a draft and must not be published as a stable download.
