# Ember release policy

The first branch is a **source-first development foundation**. It must **not** publish a stable GitHub Release as a finished private AI until every mandatory item is demonstrably working and license compliant.

- [ ] All native Windows CI builds and automated tests green, with a portable bundle that starts on a **clean** Windows 10/11 install and has zero WebView2 dependency.
- [ ] LLM model weights and backend supplied with clear safe installation and checksum verification. Tested GGUF on RTX 4070 12 GB / Ryzen 5600 / 32 GB.
- [ ] True microphone capture on actual machine, independent input/output device selection, consistent RU/EN STT with WER measurements, hotplug/focus behavior.
- [ ] Reliable female natural TTS voices with A/B listening, licensing, adjustable pace/prosody, controlled voice identity and graceful fallback.
- [ ] UI visual QA 100/125/150% DPI, keyboard accessibility, animations and real-device performance.
- [ ] Multi-turn chat correct, cancellable, first-token latency and no stale session spillovers. Clean exit and crash handling.
- [ ] Per-user Windows installer to any chosen location (default suggestion `D:\Games\Ember`), full uninstall, source licenses and clear disk-size expectations.
- [ ] Validated release version, full release notes, SHA256 checksums, source ZIP/tar supplied by GitHub, license inventory and tested installer artifact.
- [ ] Security review: no local shell/file/Internet tool access except explicitly approved capabilities; all AI network HTTP is loopback-only.

GitHub Actions **development artifacts** may contain the unsigned Qt app without models, clearly marked incomplete. The main GitHub Releases page remains reserved for verified stable binaries.
