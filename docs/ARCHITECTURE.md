# Ember architecture and development sequence

**Native shell:** Qt 6 Quick + C++20 using Qt Network, Multimedia and Widgets solely for native file dialogs. Qt DLLs are deployed dynamically while respecting relevant LGPL obligations. No WebView2/Chromium or Android layer. Input/output hardware is enumerated by `QMediaDevices` and preferences are saved as opaque device IDs; removed devices invalidate selection instead of silently accessing an unrelated device. Native capture is capped by a watchdog and by a 30-second per-recording timer with a session generation check.

**Logical layers:**
```text
QML native workspace
  ├─ Voice: explicit 16 kHz mono capture -> WAV -> whisper-cli
  │                                     -> text in current session
  ├─ Local LLM: llama-server process (127.0.0.1:43821)
  │               JSON chat completion with fixed local model alias
  └─ Voice output: native switchable providers
       ├─ sherpa-onnx CLI + Kokoro -> WAV
       └─ localhost only (127.0.0.1:43822) OpenAI speech API -> bounded PCM16 WAV
       -> selected QAudioOutput / QMediaPlayer
         Shared C++ backend / bounded in-memory transcript / portable settings
```

### Modular Frankenstein approach

Use **component composition and measurable routing** rather than unsupported claims of combining weights. ASR, text reasoner, expressive TTS, speaker embeddings and optional VAD can each use different models. We will benchmark and replace the weakest stage. Model merges, fine-tuning, speculative decoding and cascades are separate experiments that need license compatibility, GPU memory budgets and A/B baselines. There is no known safe one-click method to merge unrelated model architectures and retain all strengths. A natural voice depends on prosody, timing, punctuation, phonemization and listening tests, not just a parameter count.

### Hardware operating assumptions

A single 12 GB RTX 4070 should prioritize a quantized ~8B-class language model and reserve headroom for context KV and graphics. ASR and small ONNX TTS may run on CPU while the LLM occupies GPU memory, trading latency for lower peak VRAM. An optional Qwen3/Chatterbox speech service must be monitored for **combined** VRAM, not just single-model fit. The native API adapter blocks redirects and nonloopback URLs, validates 16-bit PCM WAV containers and caps response bytes. Actual VRAM, token/s, first-phoneme latency, idle watts and peak memory must be benchmarked on the real Ryzen 5600 machine. The current starter does not claim a verified measured FPS or latency. Microphone preferred formats now convert through a bounded native mono 16 kHz windowed-sinc resampler; this path is unit-tested with synthetic stereo/float samples, not yet recorded Windows hardware.

### Sequence and acceptance

1. **Voice quality:** real mic capture, accurate transcription in EN and RU, candidate female voices, text-to-speech evaluation, playback/cancellation. Current v0.1 foundation is turn-based, English Kokoro only, and requires user-provided engines.
2. **Text chat:** token streaming, stop generation, memory scope, conversation navigation, model selection, sampling parameters, structured reply rendering, session recovery, privacy-safe export.
3. **Modules:** explicit file permissions, secure local tool execution, GitHub/Google Drive connectors, audio/video inspection, image viewing and live screen perception. Never grant arbitrary file, shell or network access implicitly via a prompt. Include per-tool approval, audit and revocation.
4. **Optional future generation:** images, music, video, audio are not part of the current scope.

Provider-independent persona text is configurable locally. Ember applies no additional response redaction in its first local chat layer, but individual model weights may have training-time alignment and licenses; it is not accurate to promise any model is universally unrestricted. Safety for executable/file/network modules is a separate permission boundary, not a writing-style preference.

### Voice quality benchmarking

- Naturalness, emotional range, punctuation prosody, speech rhythm and absence of metallic/robotic artifacts judged blindly in A/B listening.
- ASR word error rate for real noise, Russian and English; interruption and speech endpointing.
- First audible response latency / total turn latency, speaker selection consistency and resource use.
- Stable operation after 100+ turns, memory bounds, audio devices unplug/replug, DPI scaling 100/125/150%.
