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
| Piper | https://github.com/OHF-Voice/piper1-gpl | Alternative fast TTS; **GPL-3.0-or-later**, not freely interchangeable with permissive code without obligations |

**No winner has been demonstrated** on the target PC. "Best" will be decided using repeatable audio quality / ASR WER / token/s / time-to-first-sound / stability / energy usage benchmarks, not project stars or anecdotal claims.
