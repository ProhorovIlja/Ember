# Ember architecture and development sequence

**Native shell:** Qt 6 Quick + C++20 using Qt Network, Multimedia and Widgets solely for native file dialogs. Qt DLLs are deployed dynamically while respecting relevant LGPL obligations. No WebView2/Chromium or Android layer. Input/output hardware is enumerated by `QMediaDevices` and preferences are saved as opaque device IDs; removed devices invalidate selection instead of silently accessing an unrelated device. The local Qt network manager explicitly bypasses system proxies, refuses redirects on health/chat/speech, and uses a brain-session generation counter to ignore stale health replies. Native capture is capped by a watchdog and by a 30-second per-recording timer with a session generation check.

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
       -> queue natural sentence/word segments if the LLM answer exceeds the engine limit
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
- First audible response latency / total turn latency, speaker selection consistency and resource use. The live UI currently exposes only completed ASR, LLM and TTS-chunk timings: these do **not** measure time-to-first-sound or end-to-end audio quality. Future benchmark instrumentation must add those measurements before claiming low latency.
- Stable operation after 100+ turns, memory bounds, audio devices unplug/replug, DPI scaling 100/125/150%.

### Text history, dictation and responsive UI implementation

`SessionStore` uses Qt SQLite (WAL, indexed sessions and message rows, text-only) and stores the archive under the executable's portable `data` directory. It restores the most recent chat on startup, supports explicit new/open/delete session actions and keeps a bounded UI message list while retaining all prior text in SQLite. The database is **not encrypted** and never synchronized to Google Drive without a future explicit module. SQLite's dynamic Windows driver is included in the actual native Qt dependency deployment.

The chat request uses llama.cpp's OpenAI-compatible server-sent events with bounded incremental parsing, UTF-8-safe assembly and 40 ms batched QML updates. The last completed/partial answer is persisted to SQLite, but arbitrary intermediary token timing is not a benchmark. Writing presets separately cover general conversation, collaborative fiction, troubleshooting and code work. Only a few recent turns are returned to the model due the active context budget: an actual long-term retrieval/summary system remains future work.

Dictation is intentionally independent of starting the LLM. QAudioSource writes to a temporary disk file rather than accumulating the whole recording in RAM. Stop/release transcribes consecutive bounded 25-second WAV windows using offline multilingual Whisper; the native UI inserts the final RU/EN text without submitting it. A 1 GiB raw capture safety limit bounds disk use. There is not yet VAD-aware segmentation, cross-window speaker diarization, or guaranteed transcription quality. A separate speech conversation path remains 30-second turn-based, with English-only TTS.

AMOLED visual system is rendered by Qt Quick (no WebView2), using pure black/dark navy surfaces and blue accents, a custom native SVG emblem and restrained conditional motion. Final visual review across Windows 10/11, scaling, real NVIDIA drivers and font availability is outstanding.

### 22 November — authenticated local inference endpoints (Windows)

Ember's default 127.0.0.1 endpoints are not inherently trustworthy: a
separate local program could bind the well-known TCP ports before llama-server
or speech.cpp starts and return a fake 200 response to /health.
The native `EmberCore::ownedLoopbackTcpListener` checks the kernel's IPv4
TCP listener owner using Win32 IP Helper `GetExtendedTcpTable` with
`TCP_TABLE_OWNER_PID_LISTENER`. Only a socket bound to **127.0.0.1**
(not a wildcard/public 0.0.0.0 binding) and owned by the specific managed
QProcess PID can qualify. Queries fail closed if the PID cannot be found,
the API fails or the binding changes.

The Windows check happens before the managed brain/speech health probe,
after a successful health response and immediately before each private
LLM/ASR/managed-TTS network request. It does not trust the historical
`brainReady`/`speechServerReady` boolean by itself. When the brain
listener no longer matches its expected PID, Ember refuses to send the
user's new prompt and resets the ready state. Native TTS and ASR refuse
the same unexpected listener change, preserving partial editable ASR text
according to the existing local recovery rules. The port-owner query adds
only built-in Windows `iphlpapi` (no new runtime download or third-party
daemon); a Windows-based QtTest uses real `QTcpServer` listeners to check
expected PID, wrong PID, 0.0.0.0 wildcard and closed ports. On unsupported
platforms this check fails closed because native Windows is the target.

A separately launched *unmanaged* compatible WAV TTS provider is still
possible, but is now **blocked by default** unless the user independently
enables **Trust external local TTS** under Voice settings. That opt-in
applies only when the managed speech server is not running; the managed
instance always requires PID ownership. The setting is explicitly
persistent and must never silently enable catalog downloads.
Allowing an external local service is a trust decision: that service may
receive text that Ember wants spoken, and PID ownership cannot be
established without having launched it. Do not call this mode private
by default.

This is defense-in-depth, not cryptographic server authentication.
Another local program with administrator/debug privileges, source DLL
injection or a precisely timed process/port-rebind race could defeat
a PID-only preflight. Release testing must exercise port squatting and
managed-process crash/restart during inference, plus check that no
content is delivered to an unrelated listener. Separate origin-bound
auth tokens or OS-level process-handle/socket pinning remain a possible
additional hardening gate.
