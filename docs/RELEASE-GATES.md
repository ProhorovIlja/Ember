# Ember release policy

## First public stable release: complete local text, RU/EN dictation and English voice

The text-first milestone is an **internal development checkpoint**, not a
publicly downloadable preview. The user requires a finished premium assistant
with an actual natural English-speaking voice and offline text, RU/EN
dictation, development and research capabilities. Consequently, the first
stable release requires all seventeen evidence checks in
`release/verify_release.py`, including **real English TTS listening
quality**, native Windows playback, interruption/audio cleanup, a legally
redistributable or honestly user-supplied complete offline voice stack, and a
clean Windows 10 no-VPN/no-first-run-download install.

Mandatory tests also include real Windows 10 RTX 4070 multilingual long
capture; Cancel / To draft / Transcribe & send; local LLM and coding tools;
explicit GitHub tool calls and Drive consent read; history durability,
correct uninstall and privacy. A synthetic CI build, protocol fixture,
developer installer or screenshot is **not** evidence of real voice prosody,
model quality or completed Windows deployment. No public previews.

## Correct Qwen3-TTS speaker-language acceptance — 23 November 2026

- [x] The actual upstream `Qwen3-TTS-12Hz-{0.6B,1.7B}-CustomVoice`
  speaker list was checked against its own model card. Vivian and Serena
  are **Chinese-native female speakers**, not native English speakers.
  Ryan and Aiden are English-native **male** speakers; Ono Anna/Sohee
  are Japanese/Korean-native female speakers. However every preset
  can synthesize English when the TTS model supports English and the
  request sets `language=en`. This is not a claim of native-English
  accent.
- [x] Native C++ `Core.cpp` inventory validation, Python GGUF
  metadata validation and actual PCM acceptance agree: validate
  English in **model-level** supported languages, require the
  **selected voice** has verified female gender and an unambiguous
  native language in that model, and reject male/unknown/duplicate
  voice IDs. A previous `speaker.language=en` condition would
  reject authentic Vivian/Serena before the first voice test.
  Qt/QML tests now use realistic zh-native female metadata.
- [ ] Listen on target Windows RTX 4070 and compare Vivian/Serena
  English quality, accent and naturalness against English-native
  licensed female voices or approved custom English Ember Voice
  material. The new speaker admission gate proves a consistent
  model, not quality or similarity to Sesame Maya.

## Concurrent local-model capacity and interactive readiness (23 November 2026)

- [x] Source-level manual `tools/combined_runtime_acceptance.py` added
  to the **complete Windows offline assembly**. It uses the exact
  preinstalled/approved llama.cpp and speech.cpp executables, holds both
  processes resident simultaneously, checks owned IPv4 localhost listeners,
  sends an English Qwen3.5 nonthinking text request, synthesizes verified
  non-silent English PCM, and checks a Russian nonthinking response with
  both models still loaded. No user text/audio is stored; both child
  processes are always stopped after the one-shot check. A simulated
  model-startup/OOM failure also checks child-process rollback.
- [ ] Run that test on the actual RTX 4070 12 GB with an approved GGUF
  and English Qwen3-TTS checkpoint. Verify both models are genuinely
  usable at the selected GPU offload/context, not just loaded
  sequentially. `--device auto` may place TTS on CPU; record actual
  GPU/CPU placements, peak VRAM, wall latency and Windows RAM usage
  separately rather than inferring GPU use from a successful HTTP reply.
- [x] Source-level `combined_runtime_acceptance.py` now requires
  actual English/Russian WAV+reference input for complete stable releases.
  Whisper runs both languages while LLM and Qwen3-TTS processes stay
  resident. Real scored WER/CER and a final text request are part of
  the *same manual invocation*; mocked success and ASR OOM cleanup tests pass.
- [ ] Run actual EN/RU ASR on a real RTX 4070 12 GB while LLM+TTS are
  loaded, recording GPU memory and response quality, and separately test
  microphone capture, user-initiated interruptions and recovered draft
  integrity. Human-rate natural English female voice. Source tests
  do not establish full-duplex barge-in, VRAM margin or naturalness.
- [ ] On real Windows test forced installer-QML failure and injected model
  hash corruption. Confirm that the isolated test-only Inno installation
  is rolled back and no GGUF/DLL remains, without removing personal chats.

## Native Image Library and multimodal release checks

- [x] Source-level local image file/clipboard normalization, content-addressed
  deduplication, on-device gallery and safe-only typed image request on localhost.
- [ ] Confirm vision GGUF/mmproj compatibility, projector startup, true image
  understanding on Windows 10 RTX 4070 and no accidental remote images.
- [x] Local full .emberpack backup roundtrips SQLite text plus image Library
  with SHA-256 integrity and nonoverwriting imported chat sessions.
- [ ] Add authenticated encryption (and opt-in Drive) for cloud-safe bundles,
  test crash recovery, restore UI/DPI and very large Library handling.
- [ ] Test drag-drop and Ctrl+Shift+V image paste on real Windows shell/DPI.
- [ ] Validate real third-party model licenses and full offline VLM weights
  before offering them in a final installer.



## No-VPN / no-first-run-network deployment — mandatory for final publication

- [ ] Assemble a signed-off inventory of ALL offline LLM, RU/EN ASR and
  English TTS model weights, engines, CUDA/Vulkan DLL dependencies and
  license/NOTICE texts. The English voice stack is mandatory for the first
  stable release, not an optional future preview. Verify
  exact SHA-256 and source provenance, and manually approve redistribution.
- [ ] Final complete installer and/or portable package ships every lawfully
  redistributable asset needed to run selected chat and dictation on
  fresh Windows without model-registry/GitHub/Hugging Face downloads.
- [ ] Any non-redistributable weights stay outside the public package, with
  locally importable files and a clear setup requirement; never pretend those
  assets were bundled or suggest license circumvention.
- [ ] Test clean Windows 10 offline installation, model loading, RU/EN dictation,
  cancel/draft/send choices, first-run no-background-fetch and GPU memory.
- [ ] Preserve settings, SQLite chats, user's manually imported weights on
  upgrade/uninstall unless explicitly asked otherwise.
- [ ] Review legal availability of at least one distribution channel for users
  in Russia, but never promise access to a specific blocked website.
- [ ] Final stable installer workflow must refuse publication without approved
  SHA-256 model/engine manifest; no preview Releases.



The first branch is a **source-first development foundation**. The unsigned development installer is already built and smoke-tested in GitHub Actions, but this does not waive real hardware and model acceptance. `release/verify_release.py` and `.github/workflows/stable-release.yml` block stable publication unless the tag is semantically versioned, an explicit reviewed acceptance manifest exists with seventeen HTTPS evidence records (including real TTS and no-VPN installation), the release source version matches the tested revision and detailed version-specific notes are present. It must **not** publish a stable GitHub Release as a finished private AI until every mandatory item is demonstrably working and license compliant.

- [ ] All native Windows CI builds and automated tests green, with a portable bundle that starts on a **clean** Windows 10/11 install and has zero WebView2 dependency.
- [ ] LLM model weights and backend supplied with clear safe installation and checksum verification. Tested GGUF on RTX 4070 12 GB / Ryzen 5600 / 32 GB.
- [ ] True microphone capture on the target PC; independent input/output selectors, persisted IDs and watchdog have been implemented but real USB/Bluetooth unplug/replug, focus/recovery and RU/EN WER still need hardware acceptance.
- [ ] Reliable female natural TTS voices with blind A/B listening, licensing, adjustable pace/prosody, controlled voice identity and graceful fallback. Evaluate both CPU Kokoro and localhost-only Qwen3/Chatterbox (8 MiB PCM16 WAV response contract), with spoken output in **English only**, with EN/RU transcription and text chat, and combined VRAM with the LLM.
- [ ] UI visual QA 100/125/150% DPI, keyboard accessibility, animations and real-device performance.
- [ ] Multi-turn chat correct, cancellable, first-token latency and no stale session spillovers. Multi-segment speech queue and current-stage timings now exist, but streaming voice output, measurable first-audio latency, clean exit and crash handling still need real hardware and fault testing.
- [ ] Per-user Windows installer to any chosen location (default suggestion `D:\Games\Ember`), full uninstall, source licenses and clear disk-size expectations.
- [ ] Validated release version, full release notes, SHA256 checksums, source ZIP/tar supplied by GitHub, license inventory and tested installer artifact.
- [ ] Security review: no local shell/file/Internet tool access except explicitly approved capabilities; all AI network HTTP is loopback-only.

Development CI now runs native builds, installer round-trip checks and quality-gate tests **without uploading user-facing previews**. The main GitHub Releases page remains reserved for complete verified stable binaries, release notes and hashes.

### Additional current acceptance checks

- [x] Native Windows streaming PCM handles output-format fallback (mono/stereo,
  Int16/Float, sample-rate conversion) with bounded memory and packet-alignment
  regression tests; physical DAC hotplug/latency/voice-listening not yet tested.
- [x] Private cancel closes the microphone file before removal and clears
  WAV/ASR scratch outputs, with bounded and generation-scoped Windows retries;
  regression confirms it never deletes unrelated scratch files.
- [ ] Real EN/RU dictation using 30-second and multi-minute microphone samples, including hardware hotplug and low-disk conditions; confirm no text is sent automatically and no audio remains in persistent chat history.
- [ ] SQLite crash recovery, local chat reopen/navigation/delete, Win10 file permissions, backup/export and archive growth checks. Current archive is plaintext; verify uninstall does not silently erase it.
- [ ] Long-form story coherence and recall over multiple sessions, dialogue naturalness, factual troubleshooting quality, code reliability, token-stream interruption and context overflow regression tests.
- [ ] AMOLED blue UI/icon at 100/125/150/200% DPI, keyboard interaction and a real RTX 4070 visual/performance review.
- [ ] No development previews are published; only an accepted stable Release can be downloadable under the release gating policy.

### Latest local-first implementation acceptance

- [x] Verified Windows desktop source has a bounded RU/EN transcription-segment merger with 500 ms overlap, plus tests. Physical microphone WER/long-capture tests remain unchecked.
- [x] Bounded pause-aware chunk seams on long voice AND dictation recordings,
  with full-frame slicing and synthetic quiet/noisy/continuous-speech regression
  tests; human RU/EN WER comparison is still a separate acceptance gate.
- [x] Manual per-session continuity-memory notes in SQLite with isolated sessions, disk persistence and Markdown export. Long-story narrative quality and semantic retrieval remain unchecked.
- [x] Local LLM TTFT and Qt playback-start timing hooks added. True acoustic first-audio and full-duplex / interruption latency remain unchecked.
- [ ] Confirm no temporary raw PCM/WAV leak following interrupted Whisper process, forced process kill or low disk space on Windows 10/11.
- [ ] Clean Windows / RTX 4070 visual QA of the conversation Memory modal, 100–200% DPI, keyboard-only accessibility and 24h+ session stability.

### Native model serving and spoken-output checks

- [x] User-requested startup/shutdown of managed speech.cpp using fixed loopback host and port, explicit model-download warning, optional alternative local HTTP provider and readiness poll. Hardware readiness behavior is untested; CI compilation is not a substitute.
- [x] Two private WAV speech slots with one-chunk prefetch while Windows plays the previous chunk; cleanup on normal completion/errors/cancel and bounded speech formatting. Test with actual Windows output devices.
- [x] Speech rendering removes Markdown URLs/code fences while preserving the exact text chat; reject malformed WAV byte rates/unaligned PCM. Predominantly Cyrillic voice output is skipped rather than pronounced in Russian.
- [ ] Benchmark actual Qwen3-TTS/Kokoro/Chatterbox female English voices, cold/warm model loads, VRAM co-residency, p50/p95 end-to-end spoken latency and real audio clipping/underruns on target RTX 4070.
- [x] Add privacy-preserving in-RAM PCM16 signal health diagnostics (leading/trailing digital silence, clipping, level, DC offset) with deterministic synthetic mock-server tests; no claim of audible voice ranking.
- [ ] Deliver native push-mode streaming PCM output with bounded buffering, first audible audio as soon as a model emits it, safe device-change/cancel handling, and robust fallback. Do not label current WAV playback as streaming audio.

### Managed PCM streaming implementation gate

- [x] Source path added: `speech.cpp` managed TTS now streams raw PCM16 to `QAudioSink` without first saving a full WAV, validates local endpoint/rate/content-type, applies bounded producer backpressure and preserves complete-WAV support for other engines.
- [x] Voice cancellation and Windows output-device switch/hot-unplug pathways release PCM sink and reject stale HTTP replies.
- [ ] Demonstrate actual low latency on RTX 4070 with reproducible first-audible-sample timestamps; tests must include long sentences, speech faster/slower than playback, underrun recovery and audio-device hotplug.
- [ ] Verify the exact Qt 6.8.3 PCM pipeline on clean Windows 10 physical hardware, including support for 24 kHz mono PCM, and confirm no audible artifacts at chunk boundaries.

### SSE-first-sentence voice overlap 2026-11-21

- [x] Source code can commit completed SSE text sentences, synthesize those sentences while the local llama.cpp LLM is still generating, and stream native speech.cpp PCM16 to Qt output with a deterministic bounded cursor. Original text archive is unchanged.
- [x] Source tests cover SSE sentence boundary stability, no duplicate/repeated speech, long unpunctuated drafts, title abbreviations, fenced code, `<think>` exclusion and short Cyrillic-dominant speech suppression.
- [x] Long voice segment watchdog now budgets expected PCM playback duration and extra recovery margin, not a false eight-second total audio cap.
- [ ] Audit concurrency race cases with a real native speech.cpp and llama-server pair, including user cancellation mid-sentence, disk failure, synthetic HTTP disconnect and no double playback.
- [ ] Record physical first sound and per-stage latency on Ryzen 5 5600 / RTX 4070, compare listening-quality of Vivian, Serena, Kokoro and other licensed female English options; confirm no glitches from GPU contention.

### LOCAL VOICE BENCHMARK 2026-11-21

- [x] Standard-library-only localhost voice probe present, with sample-rate, PCM length, content-type, no-redirect and no-proxy protections. Mock-service self-tests are integrated into Windows CI.
- [x] Local native Early Speech setting permits choosing sentence-first low latency or full-text GPU-headroom operation with the same PCM stream transport.
- [ ] Collect real RTX 4070 results using paired Vivian/Serena/Kokoro/other approved female English voice tests and blind naturalness ratings; compare both Early Speech states under concurrent llama-server load.
- [ ] Validate actual physical acoustic first-sound timing; the probe deliberately reports only first **HTTP byte** timing.

### Local bounded story continuity (21 November 2026)

- [x] Active-session-only older-history lexical recall with bounded scan/quotes, no new index, cloud request, cross-chat read, or synthesized audio persistence.
- [x] Pinned notes preserved; retrieved history is explicitly lower-trust and never given a privileged system role; current user input remains the final message.
- [x] Regression tests for EN/RU lexical matches, chat isolation, deletion, bounded role order; local SQLite WAL/secure delete settings.
- [x] New text turns cancel still-active old PCM audio, not only the WAV player.
- [ ] Long-session latency, recall accuracy and full voice-to-text concurrency need testing on real Windows 10 + RTX 4070 and representative stories.

### Optional native Qwen3-ASR (21 November 2026)

- [x] Whisper remains default; native speech.cpp 0.6B/1.7B recognition is explicitly selected and started by the user with catalog names, loopback-only endpoint, no proxy/redirect and bounded 25-second WAV parts.
- [x] One managed native speech.cpp process can host a supported TTS model, optional ASR model, or ASR-only mode without an emulator; English-only spoken reply policy is unchanged.
- [x] Settings, native ASR cancellation and stale Whisper exit protection integrated; core tests cover only allowed native URL/model/language choices.
- [ ] Actual speech.cpp server integration, spoken RU/EN WER/CER with controlled clips, VRAM/real-time factor, cancellation and voice capture need genuine Windows 10 RTX 4070 testing.
- [x] Add local-only English/Russian WAV/reference corpus benchmark harness with bounded safe paths, no transcript persistence and per-language aggregate WER/CER; synthetic fixture tests do not pass real audio acceptance.
- [ ] Only ship redistributable engine code/weights when current model and asset licenses are independently verified; no automatic model bundle or preview Release.

### Crash-safe microphone scratch cleanup and English-native TTS (21 November 2026)

- [x] New session temp folders have an ownership `QLockFile`; on startup only stale, correctly named and marked directories are candidates for cleanup, never a live session or arbitrary path.
- [x] Native PCM speech.cpp requests explicitly specify English; generic WAV TTS servers remain backward-compatible.
- [x] Core tests reject arbitrary or traversal-like session names and validate native-English-only TTS request shape.
- [ ] Clean Windows 10 hardware crash/reboot tests verify orphan deletion, lock behavior, antivirus file locks and no impact on concurrently running Ember instances.
- [ ] Physical securely erased media is **not** claimed; encrypted-at-rest archive and optional Drive backups remain separate future work.

### Local Qwen3-ASR benchmark tool

- [x] Source-level local benchmark provides deterministic WER/CER and timing with user-provided RU/EN WAV + reference, no transcript/audio retention and no remote requests; CI mock protocol tests have a clear engine-free path.
- [ ] Run real Qwen3-ASR 0.6B, 1.7B and Whisper comparative RU/EN voice tests under the same conditions on Windows 10 RTX 4070; inspect hallucinations on silence and GPU contention.

- [x] New microphone turns stop any old still-playing PCM stream before capture, instead of permitting previous assistant audio to be recorded into the next turn.

### Native delivery model correctness and PCM tail preservation (21 November 2026)

- [x] Managed Qwen3-TTS 1.7B can opt into bounded predefined delivery instructions; 0.6B or generic server payloads never receive unsupported instructions.
- [x] English native model presets distinguish smaller 0.6B from more expressive 1.7B; existing default remains 0.6B for device headroom.
- [x] Playback volume persists locally, controls existing WAV and currently active PCM sink, and never requests Internet connectivity.
- [x] PCM duration-to-drain guard requires Qt processed audio within 3 ms of intended segment length, plus audio IdleState, not the earlier 20 ms tolerance.
- [ ] Validate subjective female English voice naturalness, pronunciation, sentence-to-sentence prosody and physical device tails on actual Windows 10 RTX 4070 headphones/speakers. Do not publish previews.

### Subaudible initial-silence measurement (21 November)

- [x] Native localhost-only voice probe reports leading low-level PCM16 frame duration (threshold 256/32768) alongside first HTTP byte, without saving audio; mock protocol tests cover split frame alignment, silent and tone samples.
- [x] Voice comparison probe can request 1.7B delivery presets but refuses unsupported instructions with 0.6B.
- [ ] Measure actual first audible output and assess whether silence suppression would improve perception without removing soft consonants. No automatic silence trimming is currently applied.

### Local ASR proper-noun vocabulary (21 November 2026)

- [x] Optional Qwen3-ASR-specific per-chat compact background terminology uses a new SQLite table with cascade delete, a 512-character UI quota and 400-character request cap, without any new network endpoints.
- [x] Tests enforce session separation, deletion, reopening, Unicode/Russian normalization and empty-value clearing.
- [ ] Assess real WER/CER impact for fictional names, software paths and rare technical terms, and guard against model hallucinations on silent recordings.

### Native RAM-only private sessions — 21 November 2026

- [x] Explicit per-process private mode isolates private conversation context from saved chats, bypasses both user and assistant SQLite append paths, prevents private export and persistent note/ASR vocabulary writes.
- [x] New chat/clear operations stay private while enabled; leaving or opening a saved session destroys private app-thread state, with no private text imported into SQLite.
- [x] English sidebar indicates private mode; no automatic Cloud/Drive upload, no WebView/Android emulation.
- [ ] Source-level persistence predicate has unit tests; additional end-to-end SQLite/WAL inspection and app crash/recording privacy tests are required before promoting this as a high-security feature.

### 22 November 2026 — text/voice streaming state acceptance

- [x] Reasoning streams from supported local chat templates are filtered
  incrementally with at most seven pending UTF-16 marker units, not full raw
  hidden-buffer retention or repeated full-response scanning. Tests include
  fragmented model tags and 200,000-character hidden thought fixtures.
- [x] A `finish_reason: "length"` SSE termination is surfaced as incomplete
  assistant output and not silently accepted as a finished story/voice turn;
  ordinary `stop` is kept. The visible-reply 16,384-character cap remains.
- [ ] Repeat with actual installed GGUF reasoning/long-story models and true
  multi-minute user input; quantify first-visible-token delay, response
  completion accuracy, CPU/VRAM and safe cancellation on the target PC.

### 22 November 2026 — explicit story continuation and female English speech

- [x] Qt Quick offers a contextual **Continue answer** action only
  when the local model actually ended with `finish_reason: length`.
  The action starts a user-approved ordinary local message, does
  not silently request more tokens, preserves an unsent composer
  draft, and is disabled during capture/generation/playback.
- [ ] Windows 10 manual GUI/DPI acceptance for the contextual action,
  including 900px window minimum, short/long conversation, private
  chat, switch session and failure/retry.
- [ ] Blind A/B of Qwen3 CustomVoice Vivian/Serena vs truly
  English-native female alternatives (and any original VoiceDesign
  voice) with identical text, real headphone playback and RTX 4070.
- [ ] Evaluate archived Chatterbox only with pinned sources and full
  dependency licenses; consider LuxTTS as a CPU/low-VRAM candidate.
  Neither is installed or benchmarked by this research document.

### 22 November 2026 — safe interrupted microphone capture recovery

- [x] Input-selection change, input-device disappearance and QtAudio source
  error now attempt editable-only dictation prefix recovery when the closed
  scratch capture passes disk-size, 100ms and complete-frame checks; invalid
  audio is discarded. Interruption flag is cleared on new take/cancel/ASR end.
- [x] Truncated **voice** commands are never auto-submitted, including on the
  1 GiB safety quota; disk-write failures bypass recovery. Deterministic 16k mono and 48k stereo QtTests
  cover integrity, too-short capture and failure conditions.
- [ ] Physical Windows 10 unplug/replug, Bluetooth loss, default-device
  switch, low-disk capture and long RU/EN dictation with ASR actually running.
  Confirm readable editable prefix, missing-ending warning, no automatic
  message, no orphaned microphone scratch and no stale QAudioSource.

### 22 November 2026 — reject other processes on local inference ports

- [x] Windows native 127.0.0.1 TCP listener-owner check included for
  managed llama.cpp and speech.cpp before readiness and text/ASR/TTS
  requests, using IP Helper API rather than trusting HTTP /health alone.
- [x] Default deny an *unmanaged* compatible WAV speech service;
  a separate persisted advanced UI permission is required, with a
  warning that another local process receives spoken text.
- [x] QtTest real local TCP listeners cover own PID, wrong PID, expired
  listener and wildcard/non-loopback binding. No additional bundled
  third-party library or remote service required.
- [ ] Physical Windows 10 tests: malicious port preemption, server crash
  and port-rebind races, failure to send old user text/recorded audio,
  full GUI trust-toggle behavior, Qt listener owner API under non-admin
  accounts, genuine multi-process speech.cpp deployment. PID checks
  do not replace future authenticated IPC/loopback bearer tokens.


### 22 November 2026 — transactional composer acceptance

- [x] Native C++ `submitText` now reports whether local network sending was
  accepted; QML clears the draft only on `true`. A rejected text or image
  request, active microphone, invalid image payload, or replaced localhost
  listener leaves the unsent composer intact.
- [x] Source changes compile through Windows native QML/Qt pipeline once
  exact-head CI is green; this is not yet a hardware acceptance test.
- [ ] On real Windows 10, verify Ctrl+Enter and button paths, invalid mmproj,
  deleted Library image, disconnected brain service, simultaneous dictation,
  legitimate accepted send and touch/keyboard focus behavior at 100–200% DPI.


### November 22 — structural voice assets now mandatory in stable packaging

A text-first development package can still be staged with approved
llama.cpp + Whisper weights. **Public stable releases cannot use that
shortcut.** The stable GitHub workflow now passes `--require-voice` to
`tools/offline_bundle.py verify` and `-RequireVoiceBundle` to the
installer wrapper. This requires:
- the approved full LLM + multilingual Whisper files and dependencies;
- `engines/speech/speech.exe` (native AMD64 PE, approved provenance/license);
- at least one approved `models/speech/qwen3-tts-0.6b.gguf` or
  `models/speech/qwen3-tts-1.7b.gguf`, with per-file SHA-256 and license
  evidence, rather than an arbitrary TTS label or a catalog-only alias.

Existing self-tests explicitly reject a text-only public package and an
unrecognized TTS path while retaining legitimate internal text-first
staging. This is a **structural safeguard**, not hardware voice acceptance:
a forged 1 MiB GGUF header can pass preflight, so real local sound
generation, Windows playback, English female voice quality and genuine
no-VPN first-run tests remain separately mandatory release evidence.
