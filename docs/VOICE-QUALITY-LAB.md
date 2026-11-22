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

## Local text-engine latency comparison (no recorded conversations)

The new tools/text_benchmark.py performs a bounded localhost-only SSE run against an already-started llama-server listening on 127.0.0.1:43821. Run it in a trusted Python environment as `python tools/text_benchmark.py --samples 8 --output text-metrics.json`. The report contains only TTFT, total time, returned token usage when provided, character throughput, simple deterministic instruction-check outcome and selected scenario names. It does not persist generated answers or chats. The prompt set includes English story continuity style, technical diagnosis, Russian output and a strict instruction check. Run the same prompts on independent model configurations to compare speed before making any quality claim.

This is a latency/protocol probe, NOT a blind evaluation of creativity, factual correctness, long-context memory or human-quality dialogue. Without server-reported completion tokens, it reports characters/s as a proxy and labels it explicitly. A dedicated RTX4070 paired voice and model benchmark still needs physical hardware; sample server claims are not Ember benchmarks.

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

### Native live input monitor and optional pause-ending experiment

A lightweight capture tap now computes scalar RMS/peak from the exact Windows PCM buffers written to the existing temporary disk-backed recording. The UI shows an input level while recording. The optional energy-based pause endpointer requires sustained input and a ~2.2 s quiet tail; it is off by default and does not claim Silero VAD, speaker separation, automatic echo rejection or confidence estimation. It must be assessed on real microphones for false-ends during dramatic pauses, quiet speech and loud background noise. The mandatory RU/EN hold-to-dictate path remains manual release and has no new timer.

## Microphone final-stop integrity fix

A manual Stop/hold-release can happen between two 100ms watchdog ticks.
The capture-stop path now checks the atomic number of successfully written
PCM bytes and rejects any short or failed file before constructing
Whisper/Qwen transcription segments. This improves reliability and avoids
calling a truncated recording successful, but it does not improve WER
without real reference-audio tests. Audio remains local and transient.

## Offline GGUF path benchmark compatibility

When speech.cpp is started with an explicit local GGUF model filename rather
than its catalog alias, its OpenAI-compatible API permits leaving the model
field out if exactly one speech model handles the endpoint. The ASR and PCM-TTS
benchmark CLI now support `--omit-model-id`, matching Ember's in-process
adapter and avoiding a false 404 when an offline model's actual server ID is a
file path. The CLI `--model` remains a local label for experimental comparisons
and to determine whether 1.7B style controls may be used. Both tools remain
loopback-only and store only latency/WER metrics, not private transcripts or
recorded WAV audio. See upstream documented request contracts:
https://github.com/nyosegawa/speech.cpp/blob/main/docs/server.md .

For 12GB RTX4070, Qwen3-TTS converted Q8 weights are 1.21GB (0.6B) or
2.29GB (1.7B) and Qwen3-ASR converted Q8 weights 0.84GB (0.6B) or
2.18GB (1.7B), according to the published on-disk catalog
https://github.com/nyosegawa/speech.cpp/blob/main/docs/models.md .
These disk sizes are NOT measured VRAM or evidence of acoustic superiority.
A reliable recommendation needs cold/warm human listening, RU/EN WER/CER,
first audible sound, TTS RTF and combined LLM+TTS+ASR VRAM traces.

## 22 November — signal health as a prerequisite, not a voice quality score

`tools/voice_benchmark.py` now keeps at most one 12 MiB decoded PCM16
response in temporary RAM and records HTTP timings **before** signal analysis,
so processing millions of Python samples cannot be counted as model synthesis
time. Per trial it reports digital-onset proxy, initial/final silence,
overall silence occupancy, longest low-level span, peak and RMS in dBFS,
clipping sample percentage and DC-offset magnitude. Aggregate reports include
p95 initial silence, p95 clipping and the count of samples in which no
above-threshold digital voice was detected. It still never stores PCM on disk,
requires a user-started loopback service, and disables proxies/redirects.

Silent and heavily clipped recordings are detectable as candidate defects, but
these metrics do **not** reveal whether the voice sounds human, reads the
correct words, uses pleasant prosody, is emotionally nuanced, or plays without
driver-level glitches. Only blind listening, matched-prompt intelligibility
(WER/CER) and actual output-device tests can establish those properties. A
quiet expressive syllable below -42 dBFS may be counted as digital silence:
do not automatically trim it. The Python mock-server test includes silence,
a 50ms leading silence followed by a tone, clipping, odd PCM frames, invalid
sample rates, catalog-ID omission and a loopback-only boundary.

Research: the speech.cpp author explicitly documents extended early digital
silence for some Qwen3-TTS 0.6B voices and distinguishes first model audio from
the scheduled playback of actual voiced content:
https://github.com/nyosegawa/speech.cpp/blob/main/docs/models/qwen3-tts.md
https://nyosegawa.com/en/posts/speech-cpp/
The reproducible open-source TTS evaluation approaches separate signal health,
ASR-derived intelligibility and human/estimated naturalness:
https://github.com/inworld-ai/open-tts-eval
https://github.com/huggingface/open-tts-leaderboard

## 22 November — repeatable bilingual ASR corpus gate

A new standard-library-only `tools/asr_corpus.py` runs on a set of
**user-supplied local** English and Russian PCM WAV recordings plus separate
reference UTF-8 text files. It accepts a bounded manifest with relative
paths, refuses traversal, symlinks, missing inputs, duplicate recordings and
non-PCM WAVs, and keeps the same restricted loopback speech.cpp transport.
It reports per-language **micro-averaged WER/CER** calculated from raw
edit counts, plus wall time and real-time factor; no recognized/reference
text, WAV samples or even local paths are written to the JSON result.
`--omit-model-id` matches file-backed GGUF startup.

Example local corpus layout:

```json
{"cases":[
  {"audio":"recordings/english-01.wav","reference":"refs/english-01.txt","language":"en"},
  {"audio":"recordings/russian-01.wav","reference":"refs/russian-01.txt","language":"ru"}
]}
```

```powershell
python tools/asr_corpus.py --manifest .\private-voice-corpus\corpus.json --model qwen3-asr-1.7b --omit-model-id --output .\asr-metrics.json
```

The JSON example is a **schema illustration**, not a supplied real audio
dataset. Synthetic CI fixture wavs are silence with a mock returned text,
so success does not imply real speech accuracy. No output transcript is
archived, but the user-owned WAV/reference files are sensitive local data
and must not be uploaded to public GitHub or Google Drive by default.
Compare both model sizes against the same recordings and measure English
accent, Russian grammar, names, mixed-language terms and long-dictation
segment boundaries.

## 22 November — controlled TTS A/B reports without fabricated scores

The local TTS probe now accepts `--seed-base 1` (cycling seeds 1,2,3
across the same four bundled prompts) and `--environment-label` to
record a **user-provided** hardware/LLM-load label instead of fingerprinting
the PC. It stores the fixed prompt-suite SHA256 (not user text), prompt
index and seed per trial to ensure reproducible pairing. Example:

```powershell
python tools/voice_benchmark.py --model qwen3-tts-1.7b --voice vivian --omit-model-id --seed-base 1 --environment-label win10-rtx4070-llm8b --samples 12 --output vivian.json
python tools/voice_benchmark.py --model qwen3-tts-1.7b --voice serena --omit-model-id --seed-base 1 --environment-label win10-rtx4070-llm8b --samples 12 --output serena.json
python tools/compare_voice_profiles.py --baseline vivian.json --candidate serena.json
```

Restart or reconfigure the single local speech model and maintain matching
background LLM load before each experiment. The comparator rejects missing
environment labels, different prompts, unequal sample counts, shifted seeds
and invalid numeric data. It intentionally reports side-by-side HTTP latency,
longest initial digital silence proxy, clipping and generation throughput,
**not** a magic overall quality rank. The environment label is a manual
assertion and cannot prove identical GPU load or driver settings. For actual
naturalness compare blind audible recordings and transcription WER/CER using
explicitly user-controlled source material on the target PC.

## 22 November — complete built-in female Qwen3 CustomVoice shortlist

The Qt native Settings panel now offers one-click female speaker IDs:
`vivian` (bright), `serena` (warm), `ono_anna` (Japanese-native/playful),
and `sohee` (Korean-native/emotive). All are known supported **Qwen3-TTS
CustomVoice speakers**. Spoken output remains explicitly English by
`language=en`, but **their original native languages differ**, so English
pronunciation, accent, naturalness and stability must be measured instead
of assuming all four are equivalent. Keep Kokoro voices as additional
English-focused baselines.

Official upstream description:
https://github.com/QwenLM/Qwen3-TTS/blob/main/README.md
https://github.com/nyosegawa/speech.cpp/blob/main/docs/models.md

## 22 November — resilient native Windows PCM output format

A compatible Windows output DAC is not necessarily able to accept the TTS
server's native mono PCM16 at 24 kHz. The native QAudioSink path now prefers
exact source format when supported, otherwise checks the selected output's
preferred Int16/Float format, followed by 48/44.1/32-kHz mono/stereo options.
It streams through a stateful mono PCM16 converter with one-sample lookahead,
exact fractional sample positioning, 16-bit or Float32 output and stereo
duplication; packet boundaries are not audible frame boundaries. Memory stays
bounded with an output-expansion-aware reply read budget and the old Qt
network backpressure. Output PCM duration, not the source mono byte count,
governs drain checks and the per-sentence stall watchdog. No WAV file, extra
cloud endpoint or post-speech file is created. Stereo/Float is a device
compatibility fallback; linear interpolation is not an audiophile-quality
band-limited resampler and should be compared on real headphones against
native output. Audio still remains English, generated by the local model.

**Acceptance still open:** actual USB/Bluetooth DAC format switching,
WASAPI-only Float fallback, underruns, cancellation/hotplug, subjective
pronunciation and low-level artifact testing on target Windows 10 hardware.
Deterministic Qt tests exercise fragmented odd-byte HTTP-like source packets,
24->48k stereo interpolation, 48->24k mono downsampling, Float32 stereo,
mono identity, truncated input and channel-aware sink drain timing.
Device format-check API: https://doc.qt.io/qt-6.8/qaudiodevice.html ,
https://doc.qt.io/qt-6.8/qaudiosink.html .

## 22 November — private microphone cancellation and live default-output recovery

Fixed the actual Windows cancellation order: previous `cancelWork` removed
`microphone.raw` while Qt's capture file was still open, and left the
`microphone.wav` and `recognition.txt` scratch outputs in the session
directory after a cancelled ASR turn. Capture is now stopped/closed before
file removal; all three precisely named local scratch files are removed.
If Windows is briefly holding a WAV through a closing ASR file reader or
multipart HTTP reply, a bounded 350-ms retry may run up to six times.
Each retry is tied to the microphone turn revision; starting a new capture
invalidates all previously scheduled deletions. The helper doesn't delete
any other files and has a real QFile/QTemporaryDir regression test.

System-default audio output now reacts to Qt's audio-device inventory change,
interrupting a stream using a removed/stale default and switching the
next response to Windows' newly selected device. Explicit saved output
selection still falls back if that selected device disappears.

Clicking Test Voice again can now interrupt its current playback/stream
without launching overlapping PCM sinks, but cannot interrupt an active
mic transcription or LLM generation. QML exposes "Retest voice" during
playback; if early streamed TTS overlaps LLM text generation, the backend
reports why the retest is unavailable instead of doing nothing. The test controls should still be
verified with physical Windows speakers and real GGUF models; successful
CI proves logic/build only, not hotplug behaviour on Bluetooth/USB hardware.

## 22 November — pause-aware long RU/EN ASR chunk boundaries

Previous multi-minute RU/EN recognition cut every 25-second mono 16kHz
segment at an exact clock boundary. If the user spoke through that cut,
Whisper/Qwen3-ASR could lose a word; 500-ms overlap was enabled only for
dictation, not long voice commands. The local Core now examines at most
1.2 seconds of decoded **original capture PCM** before a non-final chunk
boundary (25-ms windows), and moves the cut back only when a clearly quiet
span of at least 125 ms occurs within that tail. If the recording is noisy,
the audio has no distinct pause, or the chunk is final, it keeps the
unchanged fixed 25-second boundary. All seeks and cuts remain full audio
frames. The 500-ms overlap is now used in BOTH long dictation and long
spoken conversational requests, and the preexisting lexical merger removes
only 2+ matching words to avoid over-deleting legitimate repetitions.

No sound is saved in quality reports and no external service is contacted.
Source audio remains in the same transient disk-backed scratch directory.
Real accuracy and quiet-word thresholds MUST be evaluated using the local
EN/RU ASR WAV corpus harness on user-owned recordings. The heuristic
is NOT neural VAD or forced alignment and will not perfectly identify
words or speakers.

The Whisper CLI provides optional Silero VAD and context controls:
https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/README.md
The official Qwen3-ASR long-audio tools use low-energy boundaries and
language-aware chunking:
https://github.com/QwenLM/Qwen3-ASR-Toolkit
https://huggingface.co/spaces/Qwen/Qwen3-ASR/blob/main/qwen_asr/inference/utils.py

Also changed both normal and failed ASR cleanup to reuse the bounded
Windows-safe scratch deletion already used during explicit cancellation;
this prevents a transient open Whisper recognition file or native Qwen
multipart WAV handle from leaving retained temporary sensitive data.

## 22 November — multi-minute native ASR benchmark parity

The independent local `tools/asr_corpus.py` now offers opt-in
`--app-chunking` to measure a real multi-minute recording with the same
25-second target, 500-ms overlapped context, conservative low-energy
boundary heuristic, and 2+-word lexical deduplication used by Ember's
runtime. Requires source WAV **already 16 kHz mono PCM16**, because the
pure-Python lab does not silently claim to reproduce Ember's Qt sinc
microphone conversion for arbitrary Bluetooth/stereo devices. If input
differs, it refuses the experiment. Each 25-second segment is sent only
to the user-started local speech.cpp service, and the report includes
number of segments, weighted EN/RU WER/CER, original input duration and
total ASR processing-time RTF. The report never contains recorded audio,
recognized sentences, reference text or file paths.

```powershell
python tools/asr_corpus.py --manifest .\private-voice-corpus\corpus.json --model qwen3-asr-1.7b --omit-model-id --app-chunking --output asr-chunked-metrics.json
```

This permits paired comparisons (direct whole-file recognition vs the
chunked Ember path) of real Qwen3-ASR weights, *if and only if* the service
can directly accept the full original WAV. On hardware that cannot
support the whole WAV, measure chunked mode alone. The mock 32-second
English source is silence with scripted ASR replies and deliberately tests
the merger, segment count and local-only report; it is NOT evidence of
model WER, naturalness or accuracy on Russian speakers. The corpus loader
verifies the WAV's **actual frame payload**, not just the RIFF header's
declared frame count; truncated samples are rejected before making any
local model request.

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

## SSE-sentence first-voice 2026-11-21

The spoken assistant is no longer required to wait for the **complete LLM response** when a managed, health-verified native `speech.cpp` TTS service is running. `llama-server` emits OpenAI-compatible SSE `delta.content`; Ember commits only completed stable sentences and starts their English speech in the local PCM16 push-mode QAudioSink while the text model continues its answer. This overlaps the LLM and TTS pipelines, with local model weights still held in separate native processes rather than unsafe attempts at mixing incompatible weights.

`EmberCore::takeCommittedSpeech` maintains a monotonic source cursor to avoid repeating partial SSE tokens, buffers incomplete clauses, defers unfinished fenced-code blocks, skips completed code blocks with one explanatory sentence, strips literal `<think>…</think>` reasoning spans before speech and avoids splitting at common English title abbreviations. At final `[DONE]`, it flushes the unsaid tail exactly once. If the LLM fails mid-stream or the user cancels, pending speech is stopped; partial text continues to be archived as before. For English-only voice, predominantly Cyrillic fragments are not spoken, but the original text remains intact.

The LLM and speech synthesis each have independent activity state so a completed voice segment cannot mark Ember idle while another LLM sentence is still arriving. This feature is **on only when the managed native speech.cpp process is ready**. Kokoro CLI and other generic WAV API adapters retain full-response synthesis for compatibility. Audio from the PCM path is ephemeral RAM; no synthesized or captured voice is persisted in SQLite.

The PCM drain watchdog was corrected to allow the total expected audio duration **plus** an eight-second margin: an arbitrary fixed eight-second cutoff would have truncated normal long sentences.

**Pending real acceptance:** instrument microphone-release → first **acoustic** sound and compare with the old full-answer TTS route on the user's RTX 4070 12 GB; benchmark combined GGUF/TTS VRAM and natural prosody, hotplug and overnight stability. SSE-first-sentence audio is not full-duplex and does not support mic barge-in or the model revising an already-spoken sentence. Never claim these hardware results based solely on QtTest/Windows CI.

References: https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md and https://github.com/nyosegawa/speech.cpp/blob/main/docs/server.md.

## LOCAL VOICE BENCHMARK 2026-11-21

A new standard-library-only development probe, `tools/voice_benchmark.py`, measures a **user-started native local speech.cpp** service without saving generated audio. It requests the same `response_format:pcm`/`stream_format:audio` contract as Ember, validates `X-Sample-Rate`, signed PCM16 frame alignment and 12 MiB output limits, disables proxies/redirects, and accepts only loopback HTTP. Windows CI invokes `python tools/voice_benchmark.py --self-test` against a mock localhost server, including malformed sample-rate/frame cases, so the client never needs an external benchmark API.

After installing local model binaries/weights on the **target RTX 4070 PC** and starting the TTS service in Ember, run:

```powershell
python tools/voice_benchmark.py --voice vivian --samples 12 --output .\benchmark-vivian.json
python tools/voice_benchmark.py --voice serena --samples 12 --output .\benchmark-serena.json
```

The probe cycles fixed neutral, expressive, technical and question prompts and reports first **HTTP PCM byte** p50/p95, full synthesis wall time and audio duration/wall-time throughput per sample. It does not store PCM, microphone recordings, transcripts or private chats. The optional JSON contains only names, numeric timing and sample-rate metadata. The same machine and GPU/LLM load are needed for fair comparisons; results from a non-4070 system do not establish target performance. **These measurements are not subjective naturalness scores or acoustic speaker onset.**

An optional **Early Speech** toggle in the native Settings is persisted locally. ON: synthesize first stable SSE sentence while the LLM is generating with resident speech.cpp. OFF: wait for full text, preserving GPU headroom when LLM and TTS compete on 12 GB VRAM. Both modes continue to stream PCM from the managed native server once synthesis begins.

### 21 November — native ASR objective measurement protocol

New `tools/asr_benchmark.py` measures local Qwen3-ASR 0.6B/1.7B through speech.cpp's multipart WAV `/v1/audio/transcriptions` contract. It is Python-standard-library-only, uses an immutable loopback target, validates 25 MiB input WAV and 64 KiB reply limits, and computes case-folded word and character error rates from a user-provided transcript. It prints/saves **metrics only**, never persists recognized output or WAV content. A mock-server CI test covers request shape, synthetic waveform, basic WER/CER and malformed files. These are not real accuracy results: actual bilingual samples must be benchmarked on the Ryzen 5600/RTX 4070 under comparable GPU load and judged for spelling of names, code, punctuation and long dictation.

### Native English delivery control and output headroom — 21 November 2026

The existing native `speech.cpp` Qwen3 CustomVoice option can now be switched between lightweight **0.6B** and expressive **1.7B** resident models via English Qt Quick presets, but models/weights are **not downloaded until the user explicitly starts the native service**. Only **1.7B** receives optional `instructions` on its streaming PCM requests: natural/model-default, warm conversational, soft/gentle, bright/curious, and controlled story narration. The 0.6B upstream expressly refuses delivery instructions; Ember does not send incompatible JSON fields to it or to generic WAV servers. Style is a repeatable locally persisted parameter, not a guarantee of perceived emotion. The payload still explicitly requests `language=en` and compatible mono PCM.

The local Windows output has a native 0–100% persisted volume slider, applied to both QAudioOutput WAV playback and the active QAudioSink streaming PCM path. The PCM drain check now uses a **3 ms** timing tolerance instead of an earlier **20 ms** near-tail allowance to reduce premature teardown near utterance ends; Qt IdleState is still required. This scheduling change is verified by deterministic unit cases but **must still be listened to** on physical output devices.

Grounded upstream source: https://github.com/nyosegawa/speech.cpp/blob/main/docs/models/qwen3-tts.md — 1.7B instructions support, 0.6B rejection, 1.21 GB and 2.29 GB Q8_0 weight files, and RTX 2080 Vulkan results (not extrapolated to RTX 4070). https://github.com/nyosegawa/speech.cpp/blob/main/docs/server.md — native JSON `instructions` and PCM streaming schema. The official upstream reference notes that 0.6B can include substantially more leading digital silence than 1.7B; listening onset cannot be inferred from time to first HTTP audio byte. No forced silence trimming was introduced, to avoid cutting soft phonemes or expressive pauses.

#### Physical listening acceptance for delivery presets

Compare Vivian and Serena on Qwen3-TTS 0.6B (native unconditioned) and 1.7B, with natural, warm, soft, curious and narration presets on the **same phrases**; judge natural pauses, inhalations, loudness, pronoun stress, dialogue differentiation, truncation, pronunciation of code/names and first sound at the actual speaker. Use numeric-only local timing benchmarks to measure first HTTP PCM byte and audio throughput; subjective ratings require blinded playback, and the user has not yet recorded them. Avoid declaring any voice objectively superior based only on vendor benchmarks.

### 21 November — first significant PCM level measurement without audio retention

The standard-library-only `tools/voice_benchmark.py` now additionally records the *length of initially low-level PCM16 audio* until the first sample with magnitude at least 256/32768 (roughly -42 dBFS peak). This diagnostic is distinct from first HTTP PCM byte and from **physical first audible sound** on the selected Windows headphone. The streaming parser tracks 16-bit sample boundaries even when HTTP chunks end on odd bytes, and saves no waveforms. Completely silent test clips return null rather than a fabricated onset; mock CI exercises a 50-ms synthetic silence followed by a synthetic tone. The benchmark also supports `--model qwen3-tts-1.7b --delivery warm|soft|bright|story` and rejects style instructions for 0.6B. Repeated trials with the exact same test phrases and voice are still required before any quality claims.

Example: `python tools/voice_benchmark.py --model qwen3-tts-1.7b --voice serena --delivery warm --samples 12 --output native-serena-warm.json`. The JSON contains numeric timing, seed-independent model/voice/style names and text-free metadata only, not PCM, recognition or user conversation.

### Per-conversation RU/EN recognition vocabulary (21 November 2026)

Optional native Qwen3-ASR now has a **per-conversation** text vocabulary for proper nouns, character names and technical words. The English Settings panel appears only for Qwen3-ASR, accepts up to 512 characters and saves them in a small `session_asr_hints` SQLite table keyed by the active session ID. When that specific conversation uses native recognition, at most 400 compact characters are added as the standard `prompt` field of the loopback-only `/v1/audio/transcriptions` multipart request; nothing is sent to external servers. The data is cleared from the appropriate table on session deletion and does not leak across chats. Whisper CLI is unchanged and never receives these native-only hints. The upstream Qwen3-ASR guide describes this prompt as background terminology, *not instructions*, but accuracy and hallucination risks still need genuine RU/EN listening tests.

Example hint for a story or development chat: `Mara, Yharnam, RTX 4070, C++, Qwen3, Анна`. It is user-maintained and optional; Ember does **not** silently extract identifiers from private Google Drive files or other chats.

## 22 November — streaming Markdown code-fence parity

Native early-speech segmentation now recognizes both Markdown fenced-code delimiters (triple backticks and triple tildes), waits for a matching closing delimiter while SSE tokens are still arriving, and substitutes a short spoken notice instead of reading code aloud. The textual assistant answer remains unchanged. Regression tests cover partial tilde fences, mismatched delimiters within code, successful completion and final partial code. This is source-level voice-safety behavior, not an acoustic or model-quality benchmark.

## 22 November — image references in English speech

When an answer contains Markdown image syntax, Ember's spoken renderer now says `an image in the text chat` rather than reading the alt text as an image analysis or as the spoken `!` marker. Ordinary Markdown links still speak their link labels. Images remain visible in unchanged written replies; this is a deterministic presentation rule, not vision-model inference. New Qt regression covers external and local image-reference syntax.

## 22 November — local paired Whisper.cpp / Qwen3-ASR EN/RU corpus comparison

The source-only `tools/compare_asr_engines.py` harness now applies **the same**
existing private 16 kHz mono PCM16 recordings, 25-second chunks, optional nearby
quiet cut and 500 ms overlap to **both** local recognition engines, using the
same lexical overlap deduplicator and scoring. Whisper uses the configured
offline CLI with explicit file-based temporary WAV input, stdout/stderr suppressed
and no shell; Qwen3-ASR uses only the already-running local
`127.0.0.1:43822/v1/audio/transcriptions` service. **Models and audio are not
distributed, fetched or sent to CI.** Per-request temporary WAV and text files
are removed on normal completion or failure; forced termination and OS swap are
still storage/privacy caveats.

Example using your own privately stored files and both separately installed engines:

```powershell
python tools/compare_asr_engines.py --manifest .\private-voice-corpus\corpus.json --whisper-exe .\engines\whisper-cli.exe --whisper-model .\models\ggml-large-v3-turbo.bin --qwen-model qwen3-asr-1.7b --omit-qwen-model-id --output .\paired-asr-metrics.json
```

Use `--whisper-vad-model <path>` only when you need to compare a Whisper
configuration using a local Silero VAD model; this is a **different treatment**
than Qwen without an equivalent VAD and must be documented as such. The common
manifest is the existing `tools/asr_corpus.py` JSON case array with local WAV
and reference text paths and language `en`, `ru` or `auto`.
No microphone normalization is simulated: supply already-normalized WAVs;
raw Windows 48k stereo audio cannot be claimed equivalent to the native Qt
resampler. The report contains only numeric WER/CER, latency and per-language
deltas; negative Qwen-minus-Whisper delta favors Qwen only on the chosen corpus,
not universally. Engines are evaluated sequentially and may have warm-cache
or GPU-contention bias; repeat and reverse order for a serious hardware study.
No "merged best voice model" is claimed from these metrics.

CI runs a deterministic fake-engine self-test to check file closure,
private-temp deletion, parameter shape, model validation, EN/RU reporting and
absence of recognized text/paths from the result. This does **not** measure
real recognition accuracy or confirm Windows audio hardware. Stable Releases
remain gated on actual physical listening, measurable RU/EN WER, benchmark and
redistribution/licensing acceptance.

## 22 November — robust early and fallback speech for Markdown code blocks

Ember's voice-only Markdown renderer now matches fenced code openings/closings
by **the same delimiter type** (`backtick` or `tilde`) and by the **entire
opening marker length**, rather than treating any three repeated characters
as the end of code. A closing fence must occupy its own line and may have
up to three spaces of indentation, following the
[CommonMark fenced-code rules](https://spec.commonmark.org/current/#fenced-code-blocks).
A stray wrong-type fence, a shorter fence embedded inside four-backtick/four-tilde
code, or three backticks inside an inline source expression must not release
hidden code into synthetic female English audio.

Both live sentence-by-sentence TTS and full-answer Kokoro/WAV fallback retain
the original textual/code answer, while speaking a single neutral
`The code example is in the text chat.` marker. Deterministic Qt regressions
cover mixed markers, four-marker fences, inline markers, 0–3-space indented
closers and nonclosers with four spaces. This is a presentation correctness
fix, **not** a measured MOS, expressiveness or first-acoustic-audio improvement.
As with other changes, this only passes the acceptance gate after exact-HEAD
Windows CI plus a later physical listening test.

## 22 November — conservative muted-microphone ASR suppression

Both offline Whisper.cpp and the optional localhost Qwen3-ASR endpoint now skip
inference for a resampled WAV only when it is canonical mono 16-kHz PCM16
from Ember and **every sample** lies within ±32/32768 peak
(approximately -60 dBFS). This cheaply avoids interpreting a completely
muted microphone/digital-zero interval as fluent speech. The native ASR
engine is not contacted for these segments; partial real dictation on either
side remains in the transcription queue. An event-loop hop processes the next
segment to avoid call-stack growth in long silent push-to-release recordings.

The filter deliberately does **not** claim to recognize human speech. Any
sample above the near-zero threshold retains the recording, including quiet
speech and most ambient noise; reducing hallucinations on noise needs a real
calibrated VAD, language-specific tests and user-controlled override.
[Whisper.cpp's CLI documentation](https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/README.md)
also warns that mostly silence or music can produce fluent phantom text.
Qt tests cover all-zero recording, below/above-threshold samples, negative
amplitude, 48-kHz mismatch and malformed WAV. No audio is saved in the voice
quality benchmark report and no public model weights are bundled.

This is a deterministic pre-inference filter for the native Windows application,
not evidence of lower measured hallucination rate on real microphones, nor an
acoustic voice-quality improvement. Keep stable release gates unchanged.

## 22 November — identical text/voice Markdown fences and interrupted long dictation

The native Qt Markdown display and both early/fallback TTS presentation paths
now share one bounded CommonMark-style fence parser for **three or more**
backticks or tildes, at most three spaces of indentation, matching closing
character and closing width at least as long as the opener. A shorter or wrong
marker stays as visible escaped source code, not a closing delimiter. A
triple-backtick marker embedded within a normal sentence is not treated as an
opening fence by streaming TTS, eliminating unnecessary speech latency and
omitted narration. No external HTML/remote resource loading is enabled.
Regressions cover 3/4-character markers, mixed types, indented closing,
unclosed code and inline literal fences.

Long push-to-release RU/EN dictation now **recovers already completed recognition
segments when a later Whisper or native Qwen3-ASR chunk fails**. The joined
prefix is delivered to the **editable local text composer**, with explicit
status that the ending may be missing; it is never sent automatically.
An ordinary live voice request does not use partial recovery, avoiding a
conversation driven by a truncated command. Explicit user cancellation still
discards pending segments. This is particularly relevant to long recordings
where transcription is sequential and a single late CLI/API failure previously
discarded every earlier segment. Recovery shares existing overlap-aware
lexical dedup and keeps temporary audio transient. Tests cover bilingual
overlap and mode isolation. This is an error-path resilience improvement,
not an ASR accuracy claim or a promise of unlimited disk capture.

External behavior references:
https://spec.commonmark.org/current/#fenced-code-blocks
https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/README.md

No real target-machine microphone, acoustic quality, hallucination reduction,
GPU/VRAM contention, or complete offline licensing acceptance is implied by
CI fixtures. Stable release gates remain closed.

## 22 November — bounded external inference and anti-phase microphone resilience

The native Windows process supervisor now drains and immediately discards
merged stdout/stderr from the **local text LLM**, `whisper-cli` and
`sherpa-onnx` Kokoro child processes rather than indefinitely accumulating
unconsumed diagnostic data in Qt's QProcess buffers. Local speech.cpp was
already drained separately. The diagnostic streams are not saved or copied
into chat history; model output text still arrives through the bounded local
HTTP/ASR data paths, not process logging. This is a privacy and memory-footprint
hardening measure, not proof of a measured RAM reduction.

Each **individual offline 25-second Whisper transcription job** now has
a 180-second watchdog and each individual **Kokoro synthesis job** a
120-second watchdog. Stalled processes are killed, their timers are stopped
on normal finish, failure and user cancellation, and stale QProcess signals
are ignored. The microphone's user-controlled recording length is unchanged.
On a *late* Whisper timeout, prior successfully transcribed segments of an
explicit hold-to-dictate recording are recovered into the editable composer,
with a clear incomplete-warning, via the existing nonautomatic recovery path.
For an ordinary voice turn the incomplete text is never auto-submitted. These
deadlines are generous defaults for target high-end hardware and may not suit
slower CPU-only third-party engines; genuine on-device benchmarking is required.

Microphone resampling now detects a particularly destructive stereo input
configuration: channels capturing the same speech in opposing polarity.
At most 4096 distributed source frames are inspected; only where the ordinary
channel average's power is below one sixteenth of the strongest channel's
power and that channel has a non-negligible signal does the 16-kHz resampler
select the strongest single channel instead of cancelling the voice.
Other mono, normal stereo and one-sided stereo recordings retain the existing
average; original raw recordings are not rewritten. Windows QtTests supply
out-of-phase Int16 and Float32 audio plus ordinary single-active-channel
stereo as deterministic regression fixtures. This addresses digital phase
cancellation, **not** noise suppression, acoustic echo cancellation, true
multimicrophone beamforming or measured RU/EN WER improvements.

Implementation constraints were checked against
[Qt 6.8 QProcess signals and kill behavior](https://doc.qt.io/qt-6.8/qprocess.html)
and the upstream [whisper-cli documentation](https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/README.md).
No model binaries or weights are bundled, and release gates remain closed.

## 22 November — exclude local model thinking from visible chat and history

Some llama.cpp reasoning formats expose a separate `delta.reasoning_content`
property, while other local Qwen/DeepSeek templates return `<think>...</think>`
**inside `delta.content`** (see the
[llama.cpp server reasoning-format documentation](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
and the [Qwen3 model guide](https://huggingface.co/Qwen/Qwen3-32B/blob/main/README.md)).
Previously Ember ignored separate `reasoning_content` and did not *speak*
well-formed thinking tags, but its chat UI/SQLite could still receive the
raw inline thinking markup. That can spoil long story context and clutter
saved conversations.

The native C++ text-stream receiver now maintains a bounded, **RAM-only**
raw SSE accumulator, and computes a visible projection using
`EmberCore::visibleAssistantText()` before any QML update, live TTS queue,
lastReply storage, chat history update or SQLite write. The projection removes
complete and unterminated thinking blocks, is case-insensitive, supports
multiple blocks, and withholds 1–6 character partial opening tags at stream
boundaries so the UI cannot flash an incomplete `<thi` before it becomes
`<think>`. The projection is append-only as token deltas arrive, retaining
the streaming speech cursor's position. Raw text is never persisted and is
explicitly cleared at the end of the turn. A 16,384-character cap applies to
raw content as well as visible content: very long model thought streams
therefore fail closed rather than growing without a bound.

Qt regressions cover split opening/closing tags, hidden private reasoning,
multiple blocks, normal angle-bracket prose and RU text following tags.
This is a defensive display/history filter, not a verified guarantee that
every possible model-internal reasoning format will be recognized; changing
upstream chat templates or output protocols requires additional fixture tests.
Potential false-positive filtering of a *literal* `<think>` markup discussion
is a known trade-off for preventing reasoning disclosure.

## 22 November — constant-memory streaming redaction and token-budget correctness

The prior `visibleAssistantText(accumulatedRaw)` filter was secure against
ordinary inline `<think>...</think>` leakage, but kept all hidden text
(up to 16,384 UTF-16 code units) in RAM and reprocessed the entire reply
for each incoming SSE delta. Long reasoning or an expensive chat template
could therefore hit the raw text cap *before producing a useful answer*,
and repeatedly scanning the growing buffer needlessly delayed live TTS.

The live receiver now uses `consumeVisibleAssistantDelta(state, delta)`:
a deterministic incremental filter whose only mutable text is the current
partial opening/closing marker (at most seven UTF-16 code units).
No hidden thought tokens are accumulated. The already-existing visible reply
remains capped at 16,384 code units; the SSE frame cap still applies.
A future local model can emit large private reasoning streams without inflating
Ember's own transcript memory, and the model's internal data is not passed to
QML, TTS or SQLite. The state is reset for every new reply, completion
and cancellation; stream output is append-only for the TTS cursor.
The one-shot helper remains for unit-tested small, existing text buffers.

The same batch fixes a second ambiguity: a chat-completions stream with
`finish_reason: "length"` can still send `data: [DONE]`. The latter
terminates *transport*, not necessarily the answer's semantics. Ember
now marks responses limited by the local max token budget as incomplete,
persists an explicit note in the local assistant transcript, and does not
treat a truncated voice response as a fully spoken turn. Ordinary
`finish_reason: "stop"` is unaffected.

Deterministic Qt tests exercise 200,000-character mock hidden streams,
split tag markers, append-only visible output and reset after cancellation,
plus fragmented SSE responses with `stop` versus `length` finish reasons.
This is neither a measured multi-minute GPU inference benchmark nor a
guarantee every custom chat template uses recognized tags. Literal `<think>`
in ordinary text can still be filtered because the application intentionally
prioritizes keeping model reasoning out of visible chat.

References: the [OpenAI chat completion streaming protocol](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)
defines `finish_reason` and `[DONE]`; the
[llama.cpp server](https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md)
implements compatible chat completion streams. Nothing here changes the
requirement for actual RU/EN WER, English female TTS, RTX4070 co-resident
memory and acoustic timing measurements before release.

## 22 November 2026 — updated upstream English female voice candidate audit

Source-level public research, **not** target-system A/B validation:

- [Qwen3-TTS official project](https://github.com/QwenLM/Qwen3-TTS):
  the 12Hz 1.7B CustomVoice model has **nine named timbres** and an
  optional delivery instruction; the 0.6B CustomVoice model does **not**
  support those natural-language instructions. The project lists its
  built-in female timbres Vivian/Serena as **Chinese-native**,
  Ono_Anna as Japanese-native and Sohee as Korean-native. Its
  English-native built-in timbres Ryan/Aiden are male. These voices
  can synthesize English, but this is not proof of a natural,
  English-native-sounding female assistant. Test Vivian and Serena
  against actual native female English alternatives rather than
  assuming speaker gender implies the desired accent. The official
  [VoiceDesign 1.7B and Base voice-clone workflow](https://github.com/QwenLM/Qwen3-TTS)
  is a possible *consent-and-license-cleared, local-only* future route
  to a stable original English female timbre. Do not download
  unknown reference voices, copy existing media-character identities,
  or claim Ember already supports this workflow.
- [Resemble AI Chatterbox](https://github.com/resemble-ai/chatterbox):
  its upstream README now says the repository has been **retired
  since August 2026** and receives no maintenance/support.
  Chatterbox-Turbo remains an archived MIT source candidate for
  controlled comparisons (350M English, paralinguistic tags), not a
  recommended permanently maintained dependency. Pin its artifact
  hashes and all weights/codec licenses if independently accepted.
- [LuxTTS](https://github.com/trd-jarvis/LuxTTS): a small Apache-2.0
  code/model candidate that *upstream claims* 48-kHz generation,
  CPU operation and a sub-1-GiB VRAM footprint. Its voice cloning,
  text consistency, dependency packaging and independent native
  Windows CI are **not verified** here. A candidate for a low-overhead
  fallback, never a claimed measured improvement to existing Kokoro.
- [Kokoro](https://github.com/hexgrad/kokoro) stays a low-memory
  English-output baseline and CPU fallback; its timbre/accent
  stability is evaluated against actual native female-English
  recordings and headphone/desktop loudspeaker playback.

**Selection protocol:** evaluate identical punctuation, short dialogue,
long narrative, names/acronyms, emotional delivery and interruptions
across engines with a fixed voice and model revision. Capture real
p50/p95 input release to first device-buffer submission AND independent
acoustic onset, WER on synthesized English text, MOS/AB preference,
pronunciation and false emotion, peak resident RAM and VRAM with
a simultaneously loaded 8,192-context local LLM, and model cold/warm
load time. Audio and listening labels remain **local/private**.
The best runtime may be a switchable combination (fast Kokoro for
low latency, higher-quality speech.cpp Qwen for dialogue), not an
unsafe averaging or merging of unrelated neural network weights.
No undocumented model/model-file download at first install; every
dependency requires a pinned SHA256 and an individually reviewed
redistribution license before the final Russia offline installer.

The UI now offers **Continue answer** on explicit local LLM
`finish_reason=length`; it never schedules continuation on its
own. The request follows the current chat context and keeps any
unsent composer draft, but high-fidelity scene continuity and
no-duplicate-sentence behavior still need real GGUF testing.

## 22 November 2026 — microphone interruption and recoverable dictation

Qt 6.8 documents `QMediaDevices::audioInputsChanged`, the runtime
`QAudioSource::error()` / `state()` and `QAudioSource::stop()`
(https://doc.qt.io/qt-6.8/qaudiosource.html). Windows may report capture
device changes or input errors while Ember holds a potentially long, private,
push-to-release RU/EN dictation. Previously, both unplug and a new input
selection discarded the entire in-progress raw capture.

New source policy: `EmberBackend::interruptCapture(reason, allowRecovery)`
handles three cases. Device unplug/input-change or unexpected QtAudio source
error *may recover a dictation only*, closing Qt capture before validating the
already-written scratch file. Recovery requires a real, supported input format,
no disk-write failure, exact observed-vs-file byte count, complete interleaved
PCM frames and at least 100ms of captured data. Only then can the existing
bounded ~25-second, privacy-preserving ASR path recover transcript segments.
After successful recognition the captured prefix is inserted into the
**editable composer**, marked potentially incomplete. It is never submitted
as an AI user request. A late ASR failure continues the prior editable
prefix recovery policy and carries the microphone-interruption warning.

A voice conversation, unlike dictation, must **never** use a truncated
microphone take for automatic chat submission. Disk-damaged capture and
too-short recordings likewise remain fail-closed, with scratch cleanup.
Changing the preferred device during a dictation saves the next microphone
selection but does not invalidate already-recorded, intact bytes.
The status from an interrupted recording is no longer overwritten by a
generic “microphone selection saved” notice.

Deterministic QtTest checks `canRecoverInterruptedDictation` on 16-kHz mono
and 48-kHz stereo input, sub-100ms capture, disk failure, incomplete frames,
size mismatch and voice-command denial. CI tests do **not** emulate real
Windows USB/Bluetooth disconnect, power management, driver crashes or
audio-device focus. Those scenarios still require hands-on Windows 10
testing with the user's real microphones. Do not interpret an ASR partial
prefix as complete speech, and do not retain raw WAV in local history.

**Capture safety quota:** the 1 GiB scratch quota ends microphone capture
without an explicit user release. It is therefore handled as an involuntary
interruption: verified dictation can recover only to an editable draft, and
a partial voice request is never automatically submitted. Core tests cover
both draft-only and voice-command denial at the 1 GiB numeric boundary,
without creating a giant file.

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


### November 22: robust native chat SSE frame parsing

The native llama.cpp-compatible chat stream now accepts LF, CRLF and CR SSE line endings (including mixed delimiters and packet-split CRLF), concatenates multiple `data:` fields within one event as required by the event-stream format, ignores comment/event-only heartbeats and retains strict JSON parsing and 256 KiB buffering limits. Byte-fragmented Cyrillic and `[DONE]` behavior remain covered by tests. This repairs protocol-level interoperability; it is not evidence of human voice naturalness or GPU performance.

### November 22 — long-dictation Windows file-handle safety and UTF-8 validation

Long-form dictation processes 25-second overlapping segments, repeatedly using private `recognition.txt` and `microphone.wav`. A completed Whisper process was followed by another segment while the preceding `QFile recognition.txt` reader was still open inside the same callback. On Windows, this can prevent deleting/recreating the file, leaving a stale transcript or breaking the next segment. The reader is now fully read and explicitly closed before the next segment begins; failure to delete stale text aborts rather than silently using it.

Native Qwen3-ASR multipart requests similarly retained a live `QFile microphone.wav` through `QNetworkReply::deleteLater()`. The finished callback now closes this upload file before `transcribeNextChunk()` can overwrite it, including cancel/supersession. Both Whisper and Qwen3 paths now require canonical valid UTF-8 (with optional leading BOM, preserving Cyrillic); malformed text or embedded control bytes fail with partial-dictation recovery only, never automatic submission. A valid empty transcript continues to mean silence. New Qt regressions cover English, Russian, BOM, malformed and oversized inputs.

Verification still requires physical Windows 10 device tests and real RU/EN ASR model runs: record >50 seconds of mixed speech, include multiple 25-second seams, unplug and reconnect the microphone, and repeat for Whisper.cpp and native speech.cpp/Qwen3-ASR. CI mock tests do not measure recognition accuracy or guarantee behavior of third-party binaries.

### November 22 — deterministic chat preflight and valid token-budget voice completion

Local chat requests, including an image selected from the content-addressed Library, are now fully validated and assembled before committing a user turn to the UI, SQLite, pending-image state, voice mode or continuation state. A failed local vision payload no longer creates an orphan prompt. The separate Core regression confirms an invalid external image URL or base64 payload leaves the request unchanged; the native QML integration still needs physical Windows acceptance.

OpenAI-compatible streaming can return HTTP 200 with a well-formed `[DONE]` event and `finish_reason=length`. This means the model exhausted the requested output budget, **not** a network failure. The completed token-limited text is still marked incomplete and offered for user-triggered continuation, but an initiated English spoken reply now finishes its available words rather than being cut off purely by the token cap. Only `[DONE]` + HTTP 200 + no parser error can enable that exception: HTTP errors, malformed SSE, aborted streams and missing terminal markers continue to cancel partially streamed voice. Core regression covers the eight policy combinations.

Hardware tests remain required: verify a natural complete-sentence voice and a token-capped partial in both PCM and WAV/CLI branches, then disconnect the local brain midway and ensure no new partial voice is initiated. Developer CI is not proof of audible quality or low-latency GPU inference.
