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

## Part 41 — correction of Qwen3.5 task-specific defaults (23 November)

A review of the [official Qwen3.5-9B card](https://huggingface.co/Qwen/Qwen3.5-9B)
exposed a **substantive error in Part 40**: the previous implementation set
`temperature=0.6`, `presence_penalty=0.0` for ALL Deep tasks, but Qwen
recommends those settings specifically for *precise coding* in thinking mode.
General thinking should use `temperature=1.0`, `top_p=0.95`,
`top_k=20`, `min_p=0`, `presence_penalty=1.5`. Nonthinking general
conversation remains `0.7 / 0.8 / 20 / 0 / 1.5`. All use a repeat penalty
of 1.0 in the published reference. These are upstream baselines, not proven
optima for natural dialogue on the user's model/hardware.

The correction is now implemented in `src/Core.cpp`,
`src/EmberBackend.cpp` and `src/LocalAgent.cpp`:
- Deep *general* conversation, fiction, and research use the general-thinking
  reference rather than a low-diversity coding-specific profile;
- Deep Development mode and Deep local-project code investigations use the
  precise-coding reference;
- Voice and normal chat use the nonthinking reference by default;
- Explicit user-set text temperature/top-p still override the defaults;
- Other model architectures never inherit these Qwen3.5 settings.

When verified Qwen3.5 produces **no visible output** on a nonthinking request,
the native status now warns about likely `llama.cpp`/chat-template
incompatibility instead of a misleading generic silence error.
This warning cannot by itself establish that hidden reasoning caused silence.
No hidden reasoning is archived or spoken.

Source changes and updated C++ regression assertions have been committed in
`embermind-gguf-thinking-compat`. **They have NOT passed Qt/MSVC compilation
or real GGUF runtime tests in this branch.** No event-triggered CI was
started, no model weights trained, no public Release published.


## Part 42 — local model reasoning-mode acceptance (23 November)

**Verified runnable component:** `tools/brain_mode_acceptance.py` is a
manual, standard-library-only Python tool. It was syntax-checked under
Python 3.13 and executed via `--self-test` in the current container:
`4 payload and 9 stream-fixture checks passed`. Exact source bytes were
uploaded to the user's Drive research folder and then committed to GitHub;
the final Python source **Git blob**
`177a02e80192cb798b87e3075b0ff804b5412d96` matches the
locally tested file (`git hash-object`).

Unlike the preexisting generic `text_benchmark.py`, this protocol probe
supplies Qwen3.5's official **nonthinking / general thinking / precise
coding** sampler values and explicit `enable_thinking` choices. It checks
four synthetic prompts including Russian/English exact instructions,
stream truncation, reasoning-content leaks, visible thought markers,
completion, timing and Windows localhost listener PID. It does **not**
store model replies or personal conversations, download weights or launch
the inference server. The no-network `--self-test` mock cases verified
both acceptance and rejection behavior; actual inference has not run.

The source is indexed in README and
`docs/LOCAL-MODEL-ACCEPTANCE.md`, including exact manual Windows commands.
**Remaining blockers:** the script has not been executed live on Windows
with a real RTX 4070/Qwen3.5 GGUF, and no human listening, text-quality
or custom training result exists. No source test can close the 17
mandatory real stable-release acceptance gates.

No scheduled/automated check, public Release, CI workflow or GitHub
Actions event was triggered in this development branch.

## Part 43 — end-to-end Windows delivery integration (23 November)

The user explicitly requires a **finished, installable Ember**, not another
collection of standalone test tools. The technical priority moved to product
packaging and unified operation, without inventing a stable Release.

**New user-facing functionality in source (NOT yet native-built on this
branch):**
- Main Qt header's **Start Ember / Stop Ember** invokes new
  `EmberBackend::startLocalEngines/stopLocalEngines`. One user click manages
  the selected on-device LLM and optionally selected Qwen3-TTS/Qwen3-ASR;
  no Windows/login auto-start, cloud fallback, catalog download or schedule.
  The main Start checks for preinstalled speech GGUFs before starting any
  worker; expert per-engine controls remain in Settings.
- A FIRST run with bundled `engines/speech/speech.exe` and supported
  `models/speech/qwen3-tts-{0.6b,1.7b}.gguf` now selects native English
  voice by default (prefer installed 1.7B). An existing user's stored
  choice of Kokoro/voice is respected; an empty development app still
  defaults to Kokoro rather than guessing that Qwen weights exist.

**New complete local build path:**
- `release/build-complete-offline.ps1` consumes a **clean deployed Qt app**,
  **already locally obtained, reviewed** model/engine/license files, and an
  asset manifest with actual SHA-256, sizes, source URLs and redistribution
  approvals. It requires real native LLM + Qwen3 TTS + Whisper RU/EN ASR,
  verifies hashes, GGUF headers/family, Windows executable dependencies,
  runs on-demand real LLM and English TTS inference (unless explicitly
  opted out for `-dev` only), builds voice-required ZIP64 + full Inno
  installer, runs local install/uninstall smoke, generates SHA256SUMS.txt,
  and exposes final files only after a successful temporary staging rename.
  It does NOT download, publish or schedule any process.
- `release/build-complete-windows.ps1` additionally takes Qt 6/MSVC 2022
  installation, compiles actual Qt application + unit tests with CMake,
  deploys Qt DLLs, stages Microsoft-signed CRT and then invokes above
  full-package builder. This is a single foreground user/maintainer action,
  with no GitHub Actions or remote downloads.
- `tools/portable_zip.py --require-voice` now enforces the same TTS family
  metadata as approved Inno packages. It has synthetic fail/pass source
  assertions against fake and valid-schema Qwen3 GGUF metadata.
- `tools/offline_bundle.py` refuses debug binaries/symbols from the
  user-facing payload; `release/stage-msvc-applocal.ps1` prunes them
  from freshly deployed output. This avoids shipping test executables and
  unnecessary compiler data in multi-gigabyte packages.
- Stable SemVer is blocked until the existing 17 reviewed release gates
  and matching `release/acceptance.json` are actually present. Development
  candidates with skipped real inference cannot be represented as stable.

**Evidence boundary:** All changes above are verified in the connected
GitHub source repository. The branch has NOT been compiled/run under
Windows/Qt/MSVC here; no actual approved assets, trained EmberMind/Voice,
12 GB GPU/ASR/voice listening tests or clean offline release binary were
created. The new unified assembly is a *missing product integration now
implemented in source*, not the final product itself. Never report a
completed stable release or subjective percentages as though they were
measured.

The final obstacles are substantial: legal and accessible model/runtime
weights, user-owned speaker/training data, real Qwen3.5 training/voice
conditioning, deep multi-turn/code/tool QA, actual Windows build, VRAM/audio
measurements, end-to-end offline installation, and accepted release evidence.
