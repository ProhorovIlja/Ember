# Ember Voice Quality Lab — controlled local model comparison

Goal: determine an **actual best voice for the target Ryzen 5 5600 / RTX 4070 12 GB**, rather than announcing an unmeasured winner. This is a development protocol, not a completed benchmark.

## Compare these open, modular candidates

| Model/adapter | Reason to test | Limitation |
|---|---|---|
| sherpa-onnx English Kokoro v0.19 | 11 fixed speaker IDs, low CPU/GPU resource use, first-party C++/CLI, Apache-family licensing on original Kokoro weights | English-only pack, limited emotion/style control; speaker selection alone does not guarantee realism |
| Qwen3-TTS 0.6B-CustomVoice via separate localhost API | Ten languages including Russian, 9 official speaker timbres, small relative GPU footprint | Requires running a separate compatible server and valid voice/model aliases; GPU co-residency with 8B LLM must be measured |
| Qwen3-TTS 1.7B-CustomVoice / VoiceDesign | Style instruction, potentially richer multilingual naturalness and design | More VRAM/RAM and cold-start cost; do not assume it fits alongside the GGUF text model |
| Chatterbox Turbo / Multilingual | Emotion/prosody options and voice consistency to compare | Variants have different language support; check individual license terms and output format |
| Chatterbox Nano 110M | CPU-first English expressive TTS, useful when the RTX 4070 runs a larger GGUF | Publisher claims CPU throughput, not yet independently checked on Ryzen 5600; voice quality must be evaluated alongside Turbo |
| MOSS-TTS family | High-quality conversational prosody and long-form consistency are worth blind testing | Larger models may not coexist with an 8–9B LLM on 12 GB VRAM; claims come from the publisher's own benchmarks |
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

Russian speech output is **not required** for Ember's current product specification. The Russian prompts above are optional research controls for multilingual model analysis; the mandatory spoken voice quality gate is English female naturalness. Do not use English Kokoro v0.19 to grade Russian pronunciation. Compare Russian only on engines/models whose official cards support it.

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

- [Whisper.cpp](https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/README.md): the current engine; use **-l auto/en/ru**, pin RU or EN for very short clips, evaluate large-v3-turbo against base using matched microphone samples. Ember now enables `--vad -vm <path>` when an optional local GGML Silero model exists. Benchmark this preprocessing against no-VAD for low-volume speech and RU/EN word boundaries; it does not yet perform live streaming segmentation.
- [Qwen3-ASR](https://github.com/QwenLM/Qwen3-ASR): 0.6B / 1.7B multilingual ASR, including Russian and English. Official support is research evidence, **not** an integrated Ember backend.
- [sherpa-onnx Qwen3-ASR](https://github.com/k2-fsa/sherpa-onnx/blob/master/cxx-api-examples/qwen3-asr-cxx-api.cc): native C++ API and INT8 ONNX weights; assess CPU speed, memory/VRAM coexistence and accuracy before turning it into an Ember adapter.
- [ggml Qwen3-ASR-0.6B GGUF](https://huggingface.co/ggml-org/Qwen3-ASR-0.6B-GGUF): an additional llama.cpp-compatible quantized option, separate from the text GGUF. It cannot share the existing text model process blindly.

The speech **output** preference is English-only; Russian transcription and text responses are still required. A candidate winning WER on Russian is not automatically the best at English accents, latency or resource usage. Test the composite pipeline against each candidate rather than claiming incompatible weights can be merged losslessly.

## 21 November 2026 — implementation and shortlist update

The native client now exposes optional **GGML Silero VAD** in Settings, wired to whisper-cli for English/Russian transcription. It is not required for compatibility with older binaries. In matched tests, score both false omission of quiet words and reduction of hallucinations on silence; do not report it as an accuracy gain without recordings.

The English female speech shortlist now includes **Chatterbox Nano (110M, CPU-focused)** and **Turbo (350M)** as lighter competitors to Kokoro, plus **MOSS-TTS** as a quality-oriented research candidate. Published company-model comparisons are hypotheses, not blind-listening results on this GPU. Use the same text prompts and report cold start, warm latency, VRAM co-residency, clip duration, audio clipping, accent stability and consistency across 3+ minute dialogues. Sources: https://github.com/resemble-ai/chatterbox and https://github.com/OpenMOSS/MOSS-TTS .

A "Frankenstein" integration means **selecting and orchestrating verified specialized engines**, with separate ASR/LLM/TTS selection and stable local APIs. Incompatible checkpoints must not be averaged, concatenated, or represented as a proven merged model without architecture-aware training and quantitative evidence.

## 21 November 2026 — measurable first-token and playback-start latency

Ember now records the first nonempty LLM streaming token and the time between releasing the voice microphone and `QMediaPlayer::PlayingState` for the first spoken chunk. It also retains ASR duration, full LLM response duration and TTS chunk generation time. These numbers use local monotonic `QElapsedTimer` clocks and are not sent to a network telemetry service. PlaybackState change is **not a measured speaker/headphone acoustic onset**; it excludes unknown output buffering and DAC latency.

RU/EN long dictation now includes 500 ms overlap between consecutive 25-second audio segments and a conservative duplicate-phrase merger, with tests for Cyrillic, case differences and genuinely repeated single words. Benchmark both WER/CER at chunk boundaries and full CPU/VRAM latency; overlapping segments increase the amount of audio that must be decoded. VAD remains optional, and low-volume speech may still be missed.

Additional native voice-serving candidates to review (not automatically integrated):
- [speech.cpp](https://github.com/nyosegawa/speech.cpp): native C++ GGML Qwen3-ASR/TTS and Silero VAD with OpenAI-style local audio endpoints. Check exact Windows/CUDA build, server content type and PCM16 WAV before using the current Ember adapter.
- [qwen3-tts-cpp-streaming](https://github.com/maxhorizons/qwen3-tts-cpp-streaming): Qwen3 native C++/GGML streaming. Reported RTX 5090 performance cannot be transferred to a 12 GB RTX 4070 without benchmark evidence.
- [fr820/tts-server](https://github.com/fr820/tts-server): model-agnostic speech server with streaming chunks and backend performance scripts; verify real-model rather than mock latency.

A future latency-oriented architecture should avoid starting an offline TTS process per sentence, preserve cancel/barging controls, and transmit the first valid PCM chunk as soon as it is ready. None of these designs are proven in the present Qt playback implementation.

## Resident C++ voice serving and two-slot playback (21 November)

Source-verified from [speech.cpp](https://github.com/nyosegawa/speech.cpp) `README.md`, `docs/server.md` and `docs/models.md`: `speech serve [MODEL...] --host 127.0.0.1 --port 43822 --device auto --threads 4`, the `GET /health` readiness route, the `POST /v1/audio/speech` response, and nine case-sensitive Qwen3 voices including `vivian` and `serena`. Ember now has explicit Start/Stop controls and readiness polling for this native Windows/GGML/Vulkan option, plus alternate local TTS API providers. The upstream repository's RTX 2080 and Apple M5 timings are not measurements of the user's RTX 4070; no quantitative superiority claim has been made.

The WAV-based audio chain now generates the **next** block concurrently with playback of the **current** block. This can reduce inter-segment silence, but it does **not** start audible speech at the first LLM token. Speech-friendly rendering removes common Markdown/code syntaxes only from TTS output (not the saved answer), and corrupted byte-rate, bad PCM frame alignment or malformed RIFF chunks are rejected. Distinguish UI `PlayingState` onset from first sound at the physical headphones.

**Next benchmark-critical stage**: speech.cpp can stream 16-bit PCM with `response_format:"pcm"` and `X-Sample-Rate`; Ember's present shared local API deliberately asks for a whole WAV instead. Before adding Qt `QAudioSink` push-mode PCM streaming, test bounded queued-buffer writes, audio device changes, cancellation mid-chunk, HTTP/body limits, output sample-rate validation, and actual first-sound with headphones. The pipeline must not falsely claim live duplex.

The UI offers two explicitly named Qwen3 female voice presets for speech.cpp: `vivian` (bright, lively) and `serena` (warm, gentle). These descriptions are grounded in the original [Qwen CustomVoice speaker table](https://github.com/QwenLM/Qwen3-TTS) and speech.cpp's [lowercase speaker IDs](https://github.com/nyosegawa/speech.cpp/blob/main/docs/models.md#voices). Other third-party HTTP servers may require different voice casing; the manual Voice ID field remains editable. Neither preset is declared superior until blind local listening is complete.

## 21 November — first real PCM16 streaming playback implementation

For the **user-started, health-verified speech.cpp process only**, Ember now requests `response_format:"pcm"`, `stream_format:"audio"` and validates `Content-Type: audio/pcm` and the `X-Sample-Rate` header. Audio is streamed directly from the loopback HTTP reply into native Qt Multimedia `QAudioSink` push mode with Int16/mono, on the selected Windows output. This is different from the existing two-slot complete-WAV path: the native audio sink no longer waits for a complete response to begin playback. Other OpenAI-compatible local TTS servers retain validated WAV fallback, because not all servers implement compatible PCM streaming.

The engine enforces 8–96 kHz rates, 16-bit aligned writes, a 12 MiB maximum per response, at most 768 KiB pending PCM in application memory and a 128 KiB limit on the Qt HTTP response buffer. The response is drained at 15 ms intervals with TCP-level backpressure; the next sentence only starts after the HTTP response, buffered PCM and hardware output have completed. Output selection or disconnection cancels active PCM safely; cancellation tears down the device and aborts the HTTP request. Streaming audio is transient RAM only and never written to SQLite or persistent WAV.

This is **not yet validated low-latency audio on Windows 10/RTX 4070**. The first PCM accepted by Qt is not the first audible sample at the physical headphones. Hardware checks remain necessary for sample-rate compatibility, Bluetooth delay, underruns, hotplug/reconnect, long synth-vs-play speed differences, output volume and voice naturalness. The LLM and STT are still turn-based, not a continuous full-duplex speech-to-speech model.

Source references: [speech.cpp server PCM contract](https://github.com/nyosegawa/speech.cpp/blob/main/docs/server.md), [Qt QAudioSink push mode](https://doc.qt.io/qt-6.8/qaudiosink.html), [Qt audio overview](https://doc.qt.io/qt-6.8/audiooverview.html).
