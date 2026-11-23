# Ember — progress measured by evidence (23 November 2026)

This is an **evidence ledger**, not an invented percentage of product
completion. The previous recurring ~70–80% "foundation" and ~20–30%
"complete vision" figures were *subjective guesses* and should no longer
be displayed as if they came from a reproducible calculation. Code changes
can improve functionality without independently closing any release gate.

## What has actually been verified

| Component | Code present | Evidence verified | What has NOT been verified |
| --- | --- | --- | --- |
| Native Windows app | C++20/Qt 6 QML, chat settings, local SQLite archive | `voice-foundation` commit `bdaf13069a7d95300bed41d469bf6fbd00f1c54f` passed GitHub Actions native Windows build/unit/QML/install smoke #45193033783 | Clean personal Windows 10 install, UI/DPI/device acceptance |
| LLM integration | Managed localhost llama.cpp, SSE streamed text, bounded history/tools | Source and synthetic tests on that Windows CI commit | Real Qwen3.5/EmberMind text QA, GPU throughput, VRAM under concurrent TTS |
| Local spoken voice | Kokoro CLI and native Qwen3-TTS/speech.cpp PCM integrations | Windows compilation/synthetic protocol tests only | Maya-like naturalness, real physical sound quality, low latency and hotplug |
| RU/EN transcription | Whisper and optional Qwen3-ASR + editable dictation | Source and synthetic tests in baseline CI | Measured WER/CER and real 5+ minute RU/EN microphone tests |
| Local archives and memory | SQLite persistence, scoped/manual history, notes | Windows unit tests at baseline `bdaf1306`; newer pairing/duplicate-avoidance source in experimental branch | Real long-story recall precision and perf; latest experimental branch Windows compile |
| GitHub, Drive and project tools | Bounded read-only agents, explicitly confirmed edits/issues and Google Drive read | Source/tests in Windows CI | Arbitrary-repo autonomous coding, real user-account end-to-end on target device, robust test/retry |
| Model customization | Data preparation, QLoRA recipe/preflight, blind evaluation scripts | Python synthetic protocol checks; source scripts | **No** actual fine-tuned EmberMind or Ember Voice weights, verified trained checkpoint or blind human improvement |
| Offline distribution | Source tools for model inventory, installer and ZIP | Windows development deployment smoke/structural validators | Fully licensed LLM+ASR+TTS offline package and clean no-VPN Windows install |

The release validator at `release/verify_release.py` defines **17
mandatory acceptance checks**. In the development repository there is **no
approved `release/acceptance.json`**, so **zero of those 17 have
reviewed release evidence recorded in the accepted manifest**. This is
*not* a claim that all 17 components are absent: code and CI have already
implemented parts of several checks. It means the release cannot be
certified or published. Source CI pass != hardware/quality acceptance.

## Most recent work, not yet in the CI-verified branch

Development branch: `embermind-contextual-recall` (derived from
`voice-foundation` through the intermediate
`embermind-dialogue-quality` experiments).

- Adds manually invoked multi-turn local blind comparison
  (`tools/blind_dialogue_ab.py`): 5 synthetic dialogues, 3 turns each
  per model, without private chat inputs. Actual GGUF results absent.
- Changes `SessionStore.cpp` to retrieve a single adjacent
  user/assistant message, always in chronological order, from the same
  saved session; no extra persistent index or background activity.
- Removes duplicate recalled pairs when a user question and Ember's answer
  match the same query. This reduces wasted prompt budget for the same
  archived conversation.
- Adds native Qt regression test **source** for pair reconstruction,
  Russian/English queries, source isolation and duplicate pairs.
- Separately checked the SQLite neighbor queries and pair keys in a
  small local Python/sqlite3 fixture; this is not an actual C++ QtTest.

Until a separate native build is actually executed against this
experimental branch, its source-level fixes are **UNVERIFIED in Windows CI**.
The latest *verified* Windows CI source is still `bdaf1306`.

## Next accepted technical milestones, in useful order

1. **Native compile and complete end-to-end voice/text on the real PC.**
   Produce screenshots, machine-specific test output, local microphone
   recording/dictation and measured first sound. No fictional readiness.
2. **One trained and tested EmberMind candidate.** Acquire legally sourced
   weights and *curated* data, run manually authorized training on the
   target GPU, compare with original Qwen3.5-4B/9B by blind multi-turn
   dialogue, coding tests, RU/EN and tool selection. No blind merges.
3. **One accepted expressive Ember Voice candidate.** Fine-tune/condition
   using legally owned speech, compare Kokoro/Qwen3-TTS/other compatible
   local models by human listening and latency under LLM GPU load.
4. **Useful multi-file development workflow.** Improve grounded
   repository search, scoped tests, GitHub changes, recovery/undo and
   approved Drive access beyond source-only capability boundaries.
5. **Complete offline Windows installer.** Match pinned hashes, full
   third-party license inventory, clean Windows 10/11 acceptance,
   rollback and no automatic downloads, including geographic fallback.

This is a tracking baseline, **not a scheduled monitoring job**. No
notifications, background checks, periodic research or event-based
automation are to be created. Update only when explicitly working on a
new user request and when direct evidence warrants a status change.

Baseline build: https://github.com/ProhorovIlja/Ember/actions/runs/45193033783
Experimental code: https://github.com/ProhorovIlja/Ember/tree/embermind-contextual-recall

## Additional source change — Qwen3.5 GGUF family and sampling (Part 40)

Branch: `embermind-gguf-thinking-compat`, derived from the unverified
`embermind-contextual-recall` source. This new branch is also deliberately
isolated from the CI-triggering `voice-foundation` branch.

**Actual implementation:** `src/Core.cpp` adds a bounded, local GGUF
`general.architecture` parser that never trusts a filename or the server's
`ember` alias. It recognizes only the named Qwen3/3.5 dense/MoE families;
malformed/unknown headers fail closed. `EmberBackend` uses the checked family
for chat and native agent requests. `LocalAgent` stops submitting Qwen-only
kwargs to other models. For Qwen3.5, documented Qwen sampling defaults
distinguish thinking and nonthinking while explicit user overrides remain in
charge. C++ Qt tests cover model spoofing, unknown models, fast/deep/voice
payload shapes, tool scopes and sampling overrides.

**Not accomplished:** No actual GGUF/Qwen3.5 test on user's GPU, no Windows
MSVC compilation of THIS branch, no verified llama.cpp template support for
`enable_thinking=false`, no trained EmberMind checkpoint and no voice
listening acceptance. Upstream llama.cpp issue #20833 reports some builds
ignore this flag; do NOT mark the corresponding release gate passed.

Reference: https://huggingface.co/Qwen/Qwen3.5-9B and
https://github.com/ggml-org/llama.cpp/issues/20833
Source: https://github.com/ProhorovIlja/Ember/tree/embermind-gguf-thinking-compat

