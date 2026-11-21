# Ember release policy

The first branch is a **source-first development foundation**. The unsigned development installer is already built and smoke-tested in GitHub Actions, but this does not waive real hardware and model acceptance. `release/verify_release.py` and `.github/workflows/stable-release.yml` block stable publication unless the tag is semantically versioned, an explicit reviewed acceptance manifest exists with twelve HTTPS evidence records, the release source version matches the tested revision and detailed version-specific notes are present. It must **not** publish a stable GitHub Release as a finished private AI until every mandatory item is demonstrably working and license compliant.

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

- [ ] Real EN/RU dictation using 30-second and multi-minute microphone samples, including hardware hotplug and low-disk conditions; confirm no text is sent automatically and no audio remains in persistent chat history.
- [ ] SQLite crash recovery, local chat reopen/navigation/delete, Win10 file permissions, backup/export and archive growth checks. Current archive is plaintext; verify uninstall does not silently erase it.
- [ ] Long-form story coherence and recall over multiple sessions, dialogue naturalness, factual troubleshooting quality, code reliability, token-stream interruption and context overflow regression tests.
- [ ] AMOLED blue UI/icon at 100/125/150/200% DPI, keyboard interaction and a real RTX 4070 visual/performance review.
- [ ] No development previews are published; only an accepted stable Release can be downloadable under the release gating policy.

### Latest local-first implementation acceptance

- [x] Verified Windows desktop source has a bounded RU/EN transcription-segment merger with 500 ms overlap, plus tests. Physical microphone WER/long-capture tests remain unchecked.
- [x] Manual per-session continuity-memory notes in SQLite with isolated sessions, disk persistence and Markdown export. Long-story narrative quality and semantic retrieval remain unchecked.
- [x] Local LLM TTFT and Qt playback-start timing hooks added. True acoustic first-audio and full-duplex / interruption latency remain unchecked.
- [ ] Confirm no temporary raw PCM/WAV leak following interrupted Whisper process, forced process kill or low disk space on Windows 10/11.
- [ ] Clean Windows / RTX 4070 visual QA of the conversation Memory modal, 100–200% DPI, keyboard-only accessibility and 24h+ session stability.

### Native model serving and spoken-output checks

- [x] User-requested startup/shutdown of managed speech.cpp using fixed loopback host and port, explicit model-download warning, optional alternative local HTTP provider and readiness poll. Hardware readiness behavior is untested; CI compilation is not a substitute.
- [x] Two private WAV speech slots with one-chunk prefetch while Windows plays the previous chunk; cleanup on normal completion/errors/cancel and bounded speech formatting. Test with actual Windows output devices.
- [x] Speech rendering removes Markdown URLs/code fences while preserving the exact text chat; reject malformed WAV byte rates/unaligned PCM. Predominantly Cyrillic voice output is skipped rather than pronounced in Russian.
- [ ] Benchmark actual Qwen3-TTS/Kokoro/Chatterbox female English voices, cold/warm model loads, VRAM co-residency, p50/p95 end-to-end spoken latency and real audio clipping/underruns on target RTX 4070.
- [ ] Deliver native push-mode streaming PCM output with bounded buffering, first audible audio as soon as a model emits it, safe device-change/cancel handling, and robust fallback. Do not label current WAV playback as streaming audio.

### Managed PCM streaming implementation gate

- [x] Source path added: `speech.cpp` managed TTS now streams raw PCM16 to `QAudioSink` without first saving a full WAV, validates local endpoint/rate/content-type, applies bounded producer backpressure and preserves complete-WAV support for other engines.
- [x] Voice cancellation and Windows output-device switch/hot-unplug pathways release PCM sink and reject stale HTTP replies.
- [ ] Demonstrate actual low latency on RTX 4070 with reproducible first-audible-sample timestamps; tests must include long sentences, speech faster/slower than playback, underrun recovery and audio-device hotplug.
- [ ] Verify the exact Qt 6.8.3 PCM pipeline on clean Windows 10 physical hardware, including support for 24 kHz mono PCM, and confirm no audible artifacts at chunk boundaries.
