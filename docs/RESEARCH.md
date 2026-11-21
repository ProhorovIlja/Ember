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
