# Upstream research — 21 November 2026

Sources below are **upstream documentation** checked for the initial design, not audited or bundled dependencies. Confirm the pinned version and exact transitive/model licenses before shipping any binaries or weights.

| Component | Source | Current role / license notes |
|---|---|---|
| Qt Quick / CMake | https://doc.qt.io/qt-6/cmake-build-qml-application.html | Native GPU-driven UI; LGPLv3/commercial terms and applicable modules must be honored; deploy dynamically |
| Qt licensing | https://doc.qt.io/qt-6/licensing.html | Not all modules use identical licenses. Check dynamic deployment, user replacement and notices before final installer |
| llama.cpp | https://github.com/ggml-org/llama.cpp | MIT local C/C++ inference engine, `llama-server --alias` and loopback REST |
| whisper.cpp | https://github.com/ggml-org/whisper.cpp | MIT, WAV speech recognition CLI; multilingual model required for RU |
| sherpa-onnx | https://github.com/k2-fsa/sherpa-onnx | Apache-2.0, offline text-to-speech, C++ and CLI APIs |
| Kokoro-82M | https://huggingface.co/hexgrad/Kokoro-82M | Apache-2.0 original weights; individual conversions/data assets still require verification |
| English Kokoro CLI | https://k2-fsa.github.io/sherpa/onnx/tts/pretrained_models/kokoro.html#kokoro-en-v0-19-english-11-speakers | `--kokoro-model`, `--kokoro-voices`, `--kokoro-tokens`, `--kokoro-data-dir=espeak-ng-data`, `--sid=0..10`, `--output-filename`; official v0.19 bundle does not require `--kokoro-lexicon` |
| Qwen3-8B | https://huggingface.co/Qwen/Qwen3-8B | Apache-2.0 primary candidate; benchmark actual GGUF conversion |
| Qwen3.5-9B | https://huggingface.co/Qwen/Qwen3.5-9B | Apache-2.0 newer candidate; check llama.cpp/GGUF compatibility and speed first |
| Native Qwen3-TTS HTTP | https://github.com/gabriele-mastrapasqua/qwen3-tts/blob/main/docs/serving/api.md | Community C++ local server with OpenAI-compatible `/v1/audio/speech`; validate the exact commit, Windows CUDA compatibility and license before using |
| Qwen3 Audio API | https://github.com/second-state/qwen3_audio_api | Community Python/Rust wrapper exposing local TTS and ASR endpoints; API server itself is a *separate* process; default API compatibility must be tested end-to-end |
| Chatterbox local server | https://github.com/devnen/Chatterbox-TTS-Server | Community OpenAI speech endpoint for original/Multilingual/Turbo Chatterbox, with variants and distinct licenses/VRAM demands |
| Qwen3-TTS models | https://github.com/QwenLM/Qwen3-TTS | Qwen's 0.6B and 1.7B CustomVoice/VoiceDesign; official release supports 10 languages including Russian, and English/female voices must be A/B listened for quality |
| Chatterbox Turbo / Nano | https://github.com/resemble-ai/chatterbox | Original publisher offers 350M English Turbo and 110M English Nano. Nano is the small-CPU candidate; test how prosody/voice fidelity compares, do not assume a README speed figure matches Ryzen 5600. Review exact model/license before shipping |
| OpenMOSS MOSS-TTS | https://github.com/OpenMOSS/MOSS-TTS | Research-only higher-resource expressive speech family with published vendor-reported comparisons. 8B variants are not presumed to fit beside the primary LLM on a 12 GB GPU |
| Silero VAD for Whisper | https://github.com/ggml-org/whisper.cpp#voice-activity-detection-vad | Official whisper-cli supports --vad -vm <GGML Silero file>. Ember now uses this optionally if a valid configured local file exists; actual false positives and real latency remain unmeasured |
| speech.cpp native Vulkan | https://github.com/nyosegawa/speech.cpp | MIT repository; native Windows GGML/Vulkan Qwen3-TTS, local `speech serve`, `--host`/`--port` and `GET /health`. Model GGUF license is independent. First model launch can download weights. No end-to-end RTX 4070 tests yet |
| Qwen3-TTS streaming native C++ | https://github.com/maxhorizons/qwen3-tts-cpp-streaming | CUDA-backed C++ streaming and installed Windows build requirements, but its own published GPU benchmarks do not establish latency on RTX 4070; compare against speech.cpp rather than assuming superiority |
| Piper | https://github.com/OHF-Voice/piper1-gpl | Alternative fast TTS; **GPL-3.0-or-later**, not freely interchangeable with permissive code without obligations |

**No winner has been demonstrated** on the target PC. "Best" will be decided using repeatable audio quality / ASR WER / token/s / time-to-first-sound / stability / energy usage benchmarks, not project stars or anecdotal claims.

### Native Sesame mobile parity reference: private text thread

Official mobile listings document Incognito/privacy choices, continuous voice/text memory, sharing images and voice notes, reminders, and search cards: https://play.google.com/store/apps/details?id=com.sesame.consumer and https://apps.apple.com/us/app/sesame-personal-agents/id6756329076 . These are public functional references only; proprietary Sesame login/voices/assets/source code are not imported. Ember's native RAM-only text conversation is separate and does not guarantee removal of OS swap or temporary audio files.

## 22 November 2026: current upstream voice licensing/maintenance check

- Qwen3-TTS: https://github.com/QwenLM/Qwen3-TTS — Apache-2.0
  upstream source; model and tokenizer redistribution must be
  checked for the exact weight revisions, dependencies and notices.
  The official built-in female speaker timbres are not English-native,
  so choose English female sound by real listening tests rather than
  speaker label. The 1.7B CustomVoice has delivery instructions,
  whereas 0.6B CustomVoice omits them.
- Chatterbox: https://github.com/resemble-ai/chatterbox —
  MIT upstream archived as of August 2026; do not interpret its
  published samples as proof that an ongoing maintained Windows
  dependency exists. Weight and codec dependency provenance must
  be reviewed separately.
- LuxTTS: https://github.com/trd-jarvis/LuxTTS —
  source/model Apache-2.0 claims, light CPU/GPU profile, not tested
  in Ember, no packaging or benchmark acceptance. Evaluate it as
  potential low-resource fallback, not a shipped component.

All vendors' speed/quality figures are hardware- and protocol-specific
and cannot substitute for Ember's RTX4070 real Windows benchmark.


## 22 November 2026 — new voice inference candidates, not integrated

The native Qwen3-TTS speech.cpp PCM pathway remains the reference implementation
because its loopback API, Windows footprint and validation hooks are already
represented in Ember. Adjacent open research candidates should be evaluated
against identical recorded English female narration samples, **not** merged
blindly into the shipping binary:

- https://github.com/andimarafioti/faster-qwen3-tts — streaming Qwen3
  alternative with a documented native Windows setup. Source is MIT
  (inspect pinned dependency and model licenses separately), but different
  server streaming protocols / decoder paths need adapter and GPU profiling.
- https://github.com/KoljaB/RealtimeTTS — native Qwen streaming server and
  Windows/CUDA inference variants. May reduce first audio latency, but
  brings extra Python and audio/runtime dependencies versus Ember's C++ host.
- https://github.com/OpenMOSS/MOSS-TTS — realtime context-aware conversation
  speech variant, plus larger local speech models. Speaker realism, licensing,
  memory coexistence with the local GGUF brain, Windows CPU/GPU support, and
  per-chunk buffering remain unmeasured in Ember.
- https://huggingface.co/marcosremar2/kokoro-82m-streaming-onnx —
  exploratory smaller streaming ONNX architecture with separate frontend
  and generator; not yet demonstrated as a drop-in compatible sherpa-onnx
  production component. A fixed-voice fallback may improve cold starts
  but could sacrifice naturalness and rhythm.

Comparisons must log exact upstream commit, model/weight hash, language,
speaker/style settings, audio sample rate, real first audible sample, RTF,
VRAM, concurrent llama.cpp load, blind English female voice rating and
license provenance. NONE are integrated or shipped by this research note.
Do not enable runtime model registry downloads by default.
