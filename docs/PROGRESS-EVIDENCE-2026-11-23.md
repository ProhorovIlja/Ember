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

## Part 44 — stable package must actually include working bilingual ASR

As requested, priority remains the *whole Windows product*, not releasing
independent test fragments. Audit of the full packaging path found a
specific false-completion loophole: `build-complete-offline.ps1` ran
actual LLM and native Qwen3-TTS checks but called the offline acceptance
probe with **no** real EN/RU WAV samples. Its output explicitly stated
dictation was `NOT TESTED`. A stable package could have reached the
installer stage without that critical core capability being measured.

Changes in this branch:
1. `release/build-complete-offline.ps1` now accepts the four
   EN/RU WAV/reference files and a bounded WER threshold; a stable SemVer
   build refuses absent or incomplete audio inputs. The foreground
   `offline_acceptance.py --run` transcribes both WAVs on real bundled
   Whisper and compares with references, failing on excessive WER.
   `release/build-complete-windows.ps1` forwards all these inputs.
2. `release/build-dev-installer.ps1 -VerifyInstall -RequireOfflineBundle`
   now re-verifies **every installed** runtime/model/license byte against
   the approved manifest after actual Inno installation, rather than
   merely checking Ember.exe/Qt startup. The full voice flag additionally
   verifies installed Qwen3-TTS GGUF metadata.
3. `tools/portable_zip.py` requires approved native TTS by default
   for user-facing ZIP output. The historical text-only ZIP can be made
   only by explicitly passing `--allow-text-only-development`.
4. The main native Qt readiness text no longer treats an LLM with a
   still-loading native TTS/ASR as fully ready.
5. Managed combined sessions now track both processes beyond initial
   startup; unexpected failure of one child kills the other to avoid
   leaving an orphaned multi-GiB model process.
6. The exact source paths were checked using seven read-only
   source-contract assertions through the connected GitHub repository.
   All seven source-contract checks passed; this is **NOT** an MSVC/Qt
   compile, actual inference, synthetic Python test, or installed audio
   acceptance.

Still absent: on-target Windows build and all actual RU/EN WAV test outputs,
model weights, expressive human-approved English voice, legal redistributable
binaries, and independently approved stable release evidence. No complete
installer or Release has been produced. The development branch contains
zero GitHub Actions runs. This document records real code progress without
reusing unsupported completion percentages.

## Part 45 — eliminate the oversized full-Ember installer release blocker

This update addresses two **hard file-size constraints** that invalidate a
"one single installer EXE attached to GitHub" strategy for a fully offline
LLM + ASR + English voice distribution:
- Inno Setup / Windows effectively limits an individual Setup.exe to
  about **4 GB**; `DiskSpanning=yes` is supported for larger payloads.
- GitHub Releases requires **each asset under 2 GiB**. Its unlimited
  total Release size does not remove the per-asset limit.

Source changes in isolated branch:
1. `release/ember.iss` uses `#ifdef AppDiskSpanning` to turn on
   `DiskSpanning=yes`, `DiskSliceSize=2100000000` and
   `SlicesPerDisk=1` for a complete package. The old small developer
   installer remains without disk spanning. Large `models/` content
   uses a separate `nocompression` [Files] entry rather than redundant
   solid-LZMA2 model recompression/duplication.
2. `release/build-dev-installer.ps1` adds explicit `-DiskSpanning`
   and asks ISCC to emit numbered data slices. The installed Qt headless
   smoke test restores the caller's original QT_QPA_PLATFORM,
   QT_QUICK_BACKEND, QT_OPENGL and EMBER_SMOKE_REPORT_PATH variables
   even when the test process fails; otherwise the build shell could
   leave later GUI launches headless.
3. `tools/installer_bundle.py` is a new **locally executed**
   standard-library Python tool. Exact locally tested Git blob SHA:
   `4c64cccfe4a738c4b7054458879a7e464446e770`.
   `python -m py_compile` and
   `python ... --self-test` succeeded with output
   `installer_bundle: 9 synthetic ZIP64 slice/integrity checks passed`.
   Its protocol checks exact Inno Setup executable + contiguous
   `-1.bin .. -N.bin`, rejects missing/linked/unrecognized setup
   files, builds a single lossless ZIP64 download, and rereads every
   entry with full SHA-256 before publishing.
4. `release/build-complete-offline.ps1` now explicitly compiles the
   full Inno package with disk spanning and verified install/uninstall
   WITH all slices present, then packages one
   `Ember-VERSION-Windows-x64-installer-bundle.zip` and one
   `Ember-VERSION-Windows-x64-portable.zip`. Only these two complete
   ZIPs and `SHA256SUMS.txt` remain after removal of private temp
   Setup.exe / raw slices / old checksum. Unexpected output files
   trigger failure; final folder is still published by one successful
   move from staging.
5. `tools/large_release_asset.py` is an OPTIONAL manual under-2GiB
   GitHub release transport fallback: splits either entire ZIP into
   at most 2,000,000,000-byte pieces and a bounded SHA-256 JSON index,
   then losslessly restores/validates the full original ZIP. Exact
   locally tested Git blob SHA:
   `a53b3886242784911cc8e2f7d0fff444c42acf0e`.
   `py_compile` and `self-test` passed using real filesystem and
   synthetic bytes, including missing/tampered/reordered/extra slices,
   output overwrite refusal and exact reconstructed SHA. This is
   **not** a live multi-gigabyte upload test.
6. `src/EmberBackend.cpp`: deliberate expert Stop speech/brain
   now clears the combined-session crash cascade, so text chat remains
   available when a user explicitly stops native speech, and offline
   dictation remains available after explicit Stop brain. Unexpected
   worker exits can still stop the other combined-session worker.
7. `qml/Main.qml`: Kokoro+Whisper on-demand configuration is now
   labeled `Text brain ready · voice loads on demand` rather than
   falsely presenting untested voice as `Ember ready locally`.

**Verification:** nine connected-GitHub source-contract checks passed,
confirming compile defines, no double model inclusion, disk-spanning
handoff, ZIP64 bundle, complete-slice rule, cleanup to exactly
three final files, shell environment restoration and README wording.
These are source assertions, NOT Qt/MSVC/Inno installation evidence.
Connected Drive search for GGUF, llama-server and Ember executable
found project records but no clearly available model or engine
binary suitable for a build; this does not establish that every
possible connected folder was exhaustively enumerated.

**No final Ember edition exists yet:** the current Linux environment
has no Windows/MSVC/Qt6 build toolchain or real RTX 4070 audio stack;
a Windows installer, model weights, tested TTS naturalness, fully
offline clean installation, long text/code-agent QA, 17 accepted
release gates and live GitHub/Drive tool tests are still missing.
Neither GitHub Actions nor scheduled/automated jobs were launched.

References:
https://jrsoftware.org/inno-setup/documentation/topic_setup_diskspanning.htm
https://jrsoftware.org/inno-setup/documentation/topic_filessection.htm
https://docs.github.com/en/repositories/releasing-projects-on-github/about-releases

## Part 46 — repaired full-build script and real simultaneous model capacity gate (23 November)

**Critical blocker actually discovered:** The previous last committed
`release/build-complete-offline.ps1` was **syntactically broken**. After
constructing the installer ZIP, the `$sliceRx` regex was truncated,
followed by prematurely repeated `finally` and cleanup blocks.
A complete model installer could not be built from that source at all.
The section was rewritten into one coherent conditional publication
path and one `try/finally`. Its final staging folder contains only
the verified installer-bundle ZIP, full portable ZIP and SHA256SUMS.txt.
No existing destination is overwritten.

**Verified source-level checks** (not a Windows build):
- Four PowerShell files
  (`build-complete-windows.ps1`,
  `build-complete-offline.ps1`,
  `build-dev-installer.ps1`, `stage-msvc-applocal.ps1`)
  passed a local quote/comment/delimiter lexical check after repair;
  actual Windows PowerShell ParseFile runtime remains UNTESTED.
- The manual source-to-release entrypoint now invokes
  `System.Management.Automation.Language.Parser::ParseFile`
  on all four scripts before CMake. It also verifies local offline
  model/engine hashes + redistribution license approval and, for stable
  SemVer builds, 17 approved release gates **before** expensive native
  compilation. The native PowerShell gate is source only; not tested
  with MSVC/Qt in this container.
- Existing `tools/installer_bundle.py` and
  `tools/large_release_asset.py` were executed again with the exact
  GitHub-committed bytes from the mounted local files. Python
  `py_compile` passed for both; all existing synthetic
  packaging/round-trip/tamper tests passed. Git blobs match:
  `4c64cccfe4a738c4b7054458879a7e464446e770` and
  `a53b3886242784911cc8e2f7d0fff444c42acf0e`.
- A new **foreground, user-invoked** co-residency tool
  `tools/combined_runtime_acceptance.py` was written,
  syntax-checked and self-tested locally. Its mock-only
  self-test used stubbed sibling imports and actual
  `unittest.mock` replacements to cover invalid launch settings,
  two simultaneously resident simulated children, successful
  English/Russian text + TTS progression, and simulated speech
  startup OOM with both processes stopped in `finally`.
  No real model or Windows executable was run.
  The tool was uploaded as exact source bytes to Drive and GitHub,
  with a matching locally tested Git blob:
  `9362aa8cbed227b90eda420d45e6ee609d0f8954`.
- The combined live path keeps real approved `llama-server.exe`
  and `speech.exe` **both loaded** for two Qwen3.5
  nonthinking SSE replies in English and Russian, separated by
  native PCM synthesis. It verifies both local listener PIDs and
  worker survival throughout, then shuts down both.
  This source path is now mandatory before full
  `build-complete-offline.ps1` packaging whenever live engine
  checks are enabled. Stable versions cannot skip live checks.
  The voice device is `auto`; a pass would not prove GPU placement
  or precise peak VRAM. There is no production hardware result.

**Current reality:** A stable installable release, trained personal
EmberMind and Ember Voice, legal pinned model distributions, real
Qt/MSVC build for this branch, concurrent RTX 4070 VRAM metrics,
actual long-form conversation/ASR and audible voice validation
are all still missing. The last known historical Windows CI
pass belongs to a different branch/commit. No CI workflows,
automations, timed tasks, Release artifacts or background monitoring
were created or started in this work.

## Part 47 — full voice session ASR and failed-installer rollback (23 November)

Product focus: the finished native Ember must answer and **hear** the user
on the target 12-GB RTX 4070, with simultaneous text + speech workloads,
and must remain installable/uninstallable with multi-gigabyte model
assets. Source integration improves; no public complete binary exists.

**Real runtime gap corrected in source:** The previous one-shot
`combined_runtime_acceptance.py` verified only text and TTS simultaneously.
Separate Whisper EN/RU dictation tests ran when text/TTS were unloaded.
That could miss memory contention during a full spoken turn.

This turn added `validated_bilingual_inputs`, local
`--en-wav`, `--en-reference`, `--ru-wav`, `--ru-reference`
and `--max-wer` to that same combined acceptance script.
With actual real WAV+reference pairs, Whisper transcribes English and
Russian while the *same* two approved LLM and Qwen3-TTS workers remain
loaded, and the runner verifies both listener PIDs after each
recognition. A later Russian text turn confirms the language model
survived synthesis + ASR. Scores (WER, CER, runtime, WAV bytes) contain
no private transcript. Missing WAV sets are `NOT_TESTED`, never
`PASS`; stable packaging cannot omit them. The full
`build-complete-offline.ps1` now forwards those inputs into the
new combined test as well as the separate bilingual ASR check.

**Actual local evidence:** the Python 3 source passed
`python -m py_compile` and an isolated self-test exercising
nonmatching/incomplete inputs, mocked simultaneous LLM/TTS,
two mock Whisper invocations, mock ASR OOM, mock TTS OOM and
verified cleanup of both simulated child processes. The exact
Git blob of tested source is
`770167e94f7a84bf077709487d0863c0df1b11e5`.
It was uploaded to Drive before updating the GitHub branch.
No Whisper executable or GPU was actually run in these tests.

**Installer lifecycle gap corrected:** The full Inno smoke previously
uninstalled only after all initial QML, integrity and privacy checks
passed. If installed model hashes differed, a smoke launch failed or
a privacy check found an app-local directory, the test exited before
uninstalling, potentially leaving multiple gigabytes of temporary
test-only data. `release/build-dev-installer.ps1` now uses an outer
`try/finally` to attempt the product's uninstaller regardless of success
or failure, while restoring Qt headless environment and removing only
its own temporary diagnostic file. It then checks that `Ember.exe`
and ALL other installed model/runtime files were removed.
Unexpected residual paths are reported without destructive recursive
deletion. User's real install, imported models and SQLite chats are
never targeted by this smoke.

**Important evidence boundary:** No native Windows Qt/MSVC/Inno
execution was possible in this environment, and no genuine
LLM/TTS/Whisper weight bundle, CUDA performance, natural voice
listening, full 17-check release acceptance or final installer
was created. These are source changes and mock tests only.
No workflows, scheduled tasks, background searches or automation
were run or created.

## Part 48 — authentic Qwen3-TTS voices were blocked by our own gate

**Root cause identified using the official upstream Qwen3-TTS CustomVoice
speaker table (2026-11-23):** Vivian and Serena are *Chinese-native female*
speakers, while the native-English speakers Ryan and Aiden are male.
The model nevertheless supports English synthesis with every listed speaker.
Our native Qt `validNativeSpeechInventory`, Python
`native_gguf_metadata.py` and `native_speech_acceptance.py` all incorrectly
required `speaker.language=en AND speaker.gender=female`. Consequently
an authentic Qwen3-TTS 0.6B/1.7B CustomVoice GGUF could pass offline
file hashing but **never become Ready for the default Vivian voice**.
It was a functional startup blocker, not just a documentation difference.

**Actual source changes:**
1. `src/Core.cpp` now requires the *model* globally advertises `en`,
   each advertised speaker's native language appears in the same supported
   language inventory, every speaker ID is unique and has explicit
   male/female metadata, and the **selected** speaker is female. It no
   longer wrongly requires the selected speaker's native language = en.
   `src/Core.h` documents the corrected semantics.
2. `tools/native_gguf_metadata.py` applies the same model-level-English
   and speaker-level-gender semantics to the real bounded GGUF metadata.
   The synthetic Qwen fixture now models native-zh Vivian/Serena and
   global EN/RU/ZH. A Japanese-native female fixture with model-wide
   English support is accepted, while a model missing `en`, or
   containing only male, duplicate, unsupported-native-language or
   unrecognized speakers, is rejected.
3. `tools/native_speech_acceptance.py` accepts a native-zh female
   speaker when the loaded English-capable TTS model is verified and
   the selected gender is female. Its self-test fixture is corrected
   to `vivian, native=zh` and explicitly rejects a model with no
   global `en`, even when Vivian is female.
4. `tests/CoreTests.cpp` uses native-zh female speakers and global
   EN/RU/ZH instead of a fabricated English-native female Vivian/Serena,
   with assertions retaining male-voice rejection and accurate loaded
   Qwen inventory handling.
5. `qml/Main.qml` now displays an explicit native-Chinese-speaker
   disclosure and calls the picker female voices for English synthesis,
   rather than incorrectly describing those voices as English-native.
   No silent voice-model switch or fabricated audio-quality claim.
6. `README.md`, `docs/OFFLINE-DISTRIBUTION.md`,
   `docs/VOICE-QUALITY-LAB.md`, `docs/RELEASE-GATES.md` updated
   for real upstream language/gender semantics.

**Verified source contract:** twelve read-only checks of the connected
GitHub branch passed across C++, Qt unit-test fixtures, Python validators
and QML documentation. These checks prove source wiring, not an actual
MSVC/Qt build. No real GGUF, installed English female acoustic voice,
on-device RTX4070 listening, GPU metrics, or stable Ember release was
produced. A native-Chinese speaker *can generate* English; whether it
sounds natively fluent or like the desired Ember Voice **is unproven**
and still needs human A/B against licensed native-English female voices.

**Source evidence:**
https://github.com/QwenLM/Qwen3-TTS/blob/main/README.md
official Custom Voice Generate speaker table explicitly lists native
languages of Vivian/Serena/Ryan/Aiden, with a note that any supported
speaker may speak any supported model language.
https://github.com/nyosegawa/speech.cpp/blob/main/docs/models.md
official server model inventory and speaker IDs.

**Release boundary:** Still no actually compiled and installed full
Windows Ember package, no custom-trained EmberMind or Ember Voice,
no legal pinned GGUF asset inventory, no signed 17-gate acceptance,
and no real-device audio validation. No scheduled task, automation,
background monitor, GitHub Action or Release was started.

## Part 50 — saved-chat search relevance and Russian casing

The earlier SQLite search implementation matched the correct saved conversation
but returned only the first 160 characters of the most recent matching message.
For long voice transcripts, troubleshooting records and roleplay/stories the
query text could be thousands of characters after the start, so the visible
preview did not show why the conversation matched.

Changes in the existing branch embermind-gguf-thinking-compat:
- src/SessionStore.cpp: returned message text is bounded to the known maximum
  archived length, then the native UI preview is centered around the queried
  expression. UTF-16 excerpt edges avoid splitting surrogate pairs. The result
  is short and clipped with ellipses when contextual text was omitted.
- src/SessionStore.cpp: ordinary SQLite LIKE is ASCII-case-insensitive only.
  For non-ASCII letter searches, a bounded list of lower/upper/initial-capital
  query forms covers common Russian names and terms; each LIKE remains fully
  parameterized and literal %/_/backslash escaping is retained.
- qml/Main.qml: search sidebar items now display two lines of relevant context
  and expose a hover tooltip rather than only an ellipsized unrelated prefix.
  The user still opens the matched session; automatic jumping to an ancient
  matching turn is not yet implemented.
- tests/CoreTests.cpp: additional persisted SQLite scenarios for long
  English/Russian messages, matched excerpts, uppercase/lowercase Russian
  input, wildcard literals, separated sessions and negative matches.

Commits:
32719cbb20dc08aa0c45cac5f458bb74fab54e7a
e2cf242b565e6c1e9905be12722980bd7ead8ee4
e9a8fadb905816102e160f89eac783971a1792b8
c05da216f8922fbe03ed1a3fb58fba924aef0bda
2529447f16758fe215bf3909292352f8344ff865

Verification boundary: GitHub connector readback confirms committed source.
A Python standard-library sqlite3 reproduction of the generated query
verified the SQL and variants against synthetic records: анна/Анна/АННА
match the stored "Анна"; 100% matches a literal percent; 100_ does not
act as a wildcard. This is NOT a compiled C++ QtTest, Windows GUI check,
model inference, GPU benchmark or completed full Ember release. The build
environment has no Qt6 toolchain, no target RTX 4070, and no real approved
LLM/ASR/TTS weights. No GitHub Actions, scheduled jobs, monitors or
background automation were started.

The remaining project release gates include: a complete signed-off
self-contained offline Windows distribution; natural EN female Ember Voice;
RU/EN ASR and text model quality; concurrent GPU performance; fully
permissioned GitHub/Drive/local-file workflows, and real Win10/11 manual
functional acceptance. Never advertise these source improvements as
finished EmberMind/Ember Voice weights or a stable public release.

## Part 51 — actual jump-to-message for long local chat archives

The previous search upgrade had one important limitation: it found the correct
saved conversation but clicked into the latest 300 messages, so a match in a
very old scene/log was still inaccessible without paging repeatedly.

Implemented as source on the existing development branch:
1. `src/SessionStore.h/.cpp`: every persisted message page now carries the
   durable SQLite row ID, and search returns the ID of the SAME row used for
   the text preview (not a separately selected match). A title-only result
   carries "0" and follows ordinary latest-chat navigation.
2. Stable, session-scoped window query: `messagesAround(session,id,40,40)`
   validates row ownership and loads no more than 81 records. Indexed
   `messagesBefore`, `messagesAfter`, `hasMessageBefore`,
   `hasMessageAfter` and `latestMessageId` provide 100-row paging and
   latest-view detection without allocating the full history.
3. `src/EmberBackend.h/.cpp`: `openSessionAtMessage` returns the selected
   zero-based row index; `loadEarlierMessages` is now based on a stable
   oldest ID instead of page size, and `loadNewerMessages` and
   `showLatestMessages` are exposed to QML. Existing latest chat context
   remains the LLM's working history while the UI displays a historic
   window. A new submitted prompt returns the UI to the latest saved
   messages only after all request preflight checks pass.
4. After successful SQLite writes, `appendMessage` and
   `finishChatStream` attach the actual persisted row ID to the UI
   message; an unfinished/unsaved preview is never assigned a fake ID.
5. `qml/Main.qml`: clicking a content-search hit jumps to the exact
   selected chat message, requests list positioning after QML model
   updates, marks it with a restrained blue outline, and permits paging
   forward/returning to the latest. Title-only and invalid hits use
   separate safe paths.
6. `tests/CoreTests.cpp`: creates 470 story messages interleaved with
   another private chat to check correct latest matching ID, 81-row
   selection, ID ordering, 100-row older/newer pages, no cross-session
   leakage, deleted hits and empty sessions.

Commits:
bc13ed36862084e5626ca6fadc8239bd8078b8c6
f1dc07debe538f473b5d17d2a20a177aa60e31f3
0a82104724aeb15cab9110179aa0b792458d4af1
93c855f30f55e06df82edd5948273c5b58bb33a2
9ef74255c5668bb528aea44eae5e01130854d32b
ae5f90ae1152af7fc3d43fb590abf6f41f1d403c
e3cf8627a54830dd097baba018fc56584fd1768b
74f8fac537d7cb802a23dca0029137d0e38e9e91

Readback: all changed source files and functions were reread from the
connected GitHub branch. A standalone Python sqlite3 reproduction tested
the SQL search select + ordered cursor window against 470 interleaved
story/other-session messages: selected index 40 of 81, and subsequent
newer rows were correctly ordered; the title-only match returned ID zero.
This validates SQL reasoning, NOT C++ compilation, QSQLITE runtime,
interactive QML layout, Win10/11 audio, GPU inference or real model
quality. The current environment has no installed Qt6 SDK; no Actions or
scheduled jobs were launched.

Still blocking finished premium Ember: acquire and license genuine offline
model/engine payloads, full native clean Windows installer, real concurrent
LLM/TTS/ASR validation on the RTX4070 12GB machine, natural EN female
voice, RU/EN long ASR, tool and cloud-consent end-to-end QA, UI polish,
performance and complete release acceptance. Do not claim an end-user
installer, trained EmberMind/Ember Voice or stable public Release exists.

## Part 52 — fix end-of-chat history jumps and cap live archive paging

Audit of Part 51 found two concrete correctness failures:
1) The UI inferred whether it was showing a historical window only
   from whether the last visible row had newer SQLite messages. If the
   selected search result was itself the latest row, a 41–81-row historic
   window had no newer rows; the **Latest messages** control disappeared.
   A new user turn could therefore trigger `finishChatStream` to rebuild
   dialog context from the truncated historic window instead of the
   latest 300 messages.
2) Repeated `loadEarlierMessages`/`loadNewerMessages` calls appended
   pages indefinitely. They were bounded per query (100 records), but
   NOT bounded in aggregate in the Qt `m_messages` model. That violates
   the lightweight app requirement and risks high memory/painting cost.

Confirmed GitHub source changes:
- `src/EmberBackend.h`: separate read-only `historicalView` Q_PROPERTY,
  backed by `m_historicalWindowActive`. Unlike `hasNewerMessages`,
  it records when the live UI is browsing a historic subset regardless
  of whether the selected record is the newest persisted one.
- `src/EmberBackend.cpp`: set historic-view state on jump or manual
  older/newer paging; clear it on explicit Latest, ordinary session
  opening, new chat or an accepted foreground send. For any accepted
  new text request, reset the visible buffer to the latest persisted
  SQLite messages after all local privacy/model/request preflight gates.
  Rejecting a request preserves the current UI view/draft. Model history
  remains the latest valid turn context while browsing.
- `src/Core.h/.cpp`: shared `EmberCore::mergeVisibleHistoryPage`
  direction-sensitive helper keeping at most 300 QVariantList records
  in the UI after each user-invoked page, never modifying SQLite.
- `qml/Main.qml`: the Latest control remains visible in historic mode
  even with `hasNewerMessages==false`; Newer remains visible only when
  there actually are newer records. Newer paging anchors its scroll
  at the first new row rather than jumping over up to 99 entries.
  The matched snippet is displayed as `Text.PlainText` in the
  highlighted bubble; jump aligns top of message, not the middle
  of a huge item.
- `tests/CoreTests.cpp`: one helper test covers a 300-row UI window,
  multiple 100-row older/newer pages, strict ID ordering, empty page
  behavior, exact return to latest 701..1000, and a near-tail search
  hit with no newer rows but fewer than the full 110-message chat.

Independent verification: Python 3 stdlib sqlite3 synthetic data with 470
interleaved story/other-session messages; checked search row ID, 40-before
+ matching + 40-after SQLite paging (81 total), and bidirectional 100-row
navigation where visible records never exceeded 300 and the final exact
window was 701..1000. PASS. This is a SQL/algorithm reproduction, NOT a
compiled/executed QtTest, Windows build, graphical acceptance or large
benchmark. No Qt6 SDK, Windows MSVC or user's RTX 4070 is available here.

Commits (sequential source writes):
dfab48338c4b0b102569083a03634ce6b3a1c50d
23ba93e49894323d80adba284ad55d3f8d508d77
02afe98f3c3d5043b0e7c868603e67173de03a71
9db5fb82161118c33d402416ce05aa8cf5e478a3
a148d1a9499df73c8ddec9654cf528c5cb432d06
d37f464d1644ed5ac7ee76d3ffeb819909f00ee9
19bae9e6211b182f96b2421f9a269080fc9fe96e
e01a014d9d644096c43bed579789ed78559f8a47
1f2665124b3b010634a04802c16b6181fdb89100
e70a16d78702d524889fed1bd63ffd6556b519b9
76f812a6bfa6e23d18869a41d9184df3d93e9ecb

Still unresolved: real Qt6 compilation and user-device UI, microphone,
TTS/ASR inference, model quality, model weight selection/license, and
full tested Windows installer. These source improvements do not deliver
a trained EmberMind model, a natural female-English Ember Voice or a
finished downloadable native application. No GitHub Actions, scheduled
or recurring tasks, background monitoring or public release was started.

