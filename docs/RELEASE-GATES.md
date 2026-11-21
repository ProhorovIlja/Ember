# Ember release policy

The first branch is a **source-first development foundation**. The unsigned development installer is already built and smoke-tested in GitHub Actions, but this does not waive real hardware and model acceptance. `release/verify_release.py` and `.github/workflows/stable-release.yml` block stable publication unless the tag is semantically versioned, an explicit reviewed acceptance manifest exists with twelve HTTPS evidence records, the release source version matches the tested revision and detailed version-specific notes are present. It must **not** publish a stable GitHub Release as a finished private AI until every mandatory item is demonstrably working and license compliant.

- [ ] All native Windows CI builds and automated tests green, with a portable bundle that starts on a **clean** Windows 10/11 install and has zero WebView2 dependency.
- [ ] LLM model weights and backend supplied with clear safe installation and checksum verification. Tested GGUF on RTX 4070 12 GB / Ryzen 5600 / 32 GB.
- [ ] True microphone capture on the target PC; independent input/output selectors, persisted IDs and watchdog have been implemented but real USB/Bluetooth unplug/replug, focus/recovery and RU/EN WER still need hardware acceptance.
- [ ] Reliable female natural TTS voices with blind A/B listening, licensing, adjustable pace/prosody, controlled voice identity and graceful fallback. Evaluate both CPU Kokoro and localhost-only Qwen3/Chatterbox (8 MiB PCM16 WAV response contract), including actual Russian voice quality and combined VRAM with the LLM.
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
