# Ember Voice Quality Lab — controlled local model comparison

Goal: determine an **actual best voice for the target Ryzen 5 5600 / RTX 4070 12 GB**, rather than announcing an unmeasured winner. This is a development protocol, not a completed benchmark.

## Compare these open, modular candidates

| Model/adapter | Reason to test | Limitation |
|---|---|---|
| sherpa-onnx English Kokoro v0.19 | 11 fixed speaker IDs, low CPU/GPU resource use, first-party C++/CLI, Apache-family licensing on original Kokoro weights | English-only pack, limited emotion/style control; speaker selection alone does not guarantee realism |
| Qwen3-TTS 0.6B-CustomVoice via separate localhost API | Ten languages including Russian, 9 official speaker timbres, small relative GPU footprint | Requires running a separate compatible server and valid voice/model aliases; GPU co-residency with 8B LLM must be measured |
| Qwen3-TTS 1.7B-CustomVoice / VoiceDesign | Style instruction, potentially richer multilingual naturalness and design | More VRAM/RAM and cold-start cost; do not assume it fits alongside the GGUF text model |
| Chatterbox Turbo / Multilingual | Emotion/prosody options and voice consistency to compare | Variants have different language support; check individual license terms and output format |
| Other future adapters | Use the same narrow, auditable local WAV contract to compare alternatives fairly | Must test against real audio output, not only a README benchmark |

Qwen original models: https://github.com/QwenLM/Qwen3-TTS
Qwen API candidates: https://github.com/second-state/qwen3_audio_api and https://github.com/gabriele-mastrapasqua/qwen3-tts
Chatterbox API: https://github.com/devnen/Chatterbox-TTS-Server
Kokoro official v0.19: https://k2-fsa.github.io/sherpa/onnx/tts/pretrained_models/kokoro.html

## Listening prompts — keep identical across voices

English neutral: "The train is leaving in fifteen minutes. Would you like me to check the next connection?"
English expressive: "I thought you'd already left! Well, that's a wonderful surprise."
English intimate, not melodramatic: "I'm here. Tell me what happened, and I'll listen."
English technical: "The graphics card has twelve gigabytes of video memory. We should reserve some for the context cache."
English punctuation: "Wait... is that really what you meant? Interesting. Tell me more."
English long answer: A 3-paragraph passage with quotes, abbreviations, figures and questions; every paragraph must be voiced, not clipped after 700 characters.
Russian neutral: "Привет. Сегодня отличный день, чтобы проверить, насколько естественно звучит мой голос."
Russian emotional: "Серьёзно? Вот это неожиданность! Расскажи, как всё произошло."
Russian technical: "Проверь загрузку видеокарты, использование видеопамяти и задержку до начала воспроизведения."
Russian multilingual: "Сейчас я проверю русский текст, а затем переключусь на English for a moment."

Settings now offer an editable comparison script, saved locally in `data/ember.ini` and played through the selected voice engine. Use the same script for each candidate, and record speaker ID/model version in the benchmark notes. Tests can reproduce the exact text rather than relying on a fixed hard-coded English greeting.

Do not use English Kokoro v0.19 to grade Russian pronunciation. Compare Russian only on engines/models whose official cards support it.

## Methodology and acceptance

Run each candidate **3 warm** and **3 cold** times; record the model version, quantization, CUDA runtime, exact prompt, speaker ID, GPU memory, system memory and CPU/GPU utilization. Give each blinded A/B listening sample neutral labels; listen at matched perceived loudness and on the same output device.

Rate (1–5) naturalness, non-robotic delivery, stress/prosody, affect control, intelligibility, voice identity consistency and artifacts. Distinguish voice audio fidelity from the STT word error rate (WER). Use real user microphone samples in quiet and noisy environments, and compare EN/RU transcripts against manual reference text.

Timing: the UI shows last successful **ASR stage**, **LLM full response** and **TTS chunk generation** durations in ms. These are not first-audio latency or real-time factors. Full benchmark needs mic stop-to-first audible sound, end-of-answer, audio duration and TTS real-time factor (RTF = generation seconds/audio seconds). Verify long replies contain all text after sentence chunking.

Set explicit resource budgets for 12 GB VRAM with an 8–9B Q4 GGUF, Qt UI, CUDA backends and auxiliary voice models. When co-residency exceeds VRAM, prefer CPU Kokoro, a smaller TTS variant or alternating sessions rather than pretending two large models fit.

The resulting preference should be configurable: different voice families can win on different criteria, so assign a fast low-latency profile and a quality-first profile only after data supports the decision. Never overwrite licensed engine weights or assert that unrelated checkpoints can be merged losslessly.

## Privacy and release boundaries

Model/audio data never reaches a remote inference endpoint from the Ember client; localhost HTTP bypasses proxies and rejects redirects. A third-party server must independently be configured to bind **only** 127.0.0.1. If model software downloads weights, that is a separate external action and requires separate trust and license review.

Temporary audio is written only to Ember's private session cache; normal exit removes it, but forced termination can leave files and must be tested. No test dataset, voice reference clip or personally identifiable microphone recording should be committed to this public repository or uploaded to Google Drive without explicit intent.

**Status:** No real listening comparisons or target-PC VRAM benchmarks have been completed at this checkpoint. The development UI exists and can measure stage latency after the user supplies compatible engines and weights.

### ASR candidates added after multilingual model research

- [Whisper.cpp](https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/README.md): the current engine; use **-l auto/en/ru**, pin RU or EN for very short clips, evaluate large-v3-turbo against base using matched microphone samples. Modern Whisper includes optional `--vad` and `--vad-model` using Silero VAD; future integration should measure false segmentation and interruption cost.
- [Qwen3-ASR](https://github.com/QwenLM/Qwen3-ASR): 0.6B / 1.7B multilingual ASR, including Russian and English. Official support is research evidence, **not** an integrated Ember backend.
- [sherpa-onnx Qwen3-ASR](https://github.com/k2-fsa/sherpa-onnx/blob/master/cxx-api-examples/qwen3-asr-cxx-api.cc): native C++ API and INT8 ONNX weights; assess CPU speed, memory/VRAM coexistence and accuracy before turning it into an Ember adapter.
- [ggml Qwen3-ASR-0.6B GGUF](https://huggingface.co/ggml-org/Qwen3-ASR-0.6B-GGUF): an additional llama.cpp-compatible quantized option, separate from the text GGUF. It cannot share the existing text model process blindly.

The speech **output** preference is English-only; Russian transcription and text responses are still required. A candidate winning WER on Russian is not automatically the best at English accents, latency or resource usage. Test the composite pipeline against each candidate rather than claiming incompatible weights can be merged losslessly.
