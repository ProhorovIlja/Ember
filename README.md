# Ember

**Local-first voice and text AI for Windows.** Original native **C++20 + Qt 6 Quick** desktop UI. No WebView2, browser wrapper, Android emulator, cloud account, or compulsory subscription.

This repository currently contains a **development foundation**. It compiles an application and provides real integration points for local engines, but this is **not yet a fully tested release or a real-time, full-duplex conversation system**. No models or vendor executables are redistributed. Follow [the setup guide](docs/SETUP.md), [offline distribution plan](docs/OFFLINE-DISTRIBUTION.md) and [release gates](docs/RELEASE-GATES.md).

## Design priorities

- Speech first: choose a real Windows microphone → local `whisper.cpp` transcription → local `llama.cpp` response → either offline `sherpa-onnx` Kokoro or a separate **localhost-only** OpenAI-compatible Qwen3-TTS/Chatterbox service → selected Windows speakers.
- Explicit complete local .emberpack backup includes chats and image Library
  with SHA-256 verification. Legacy .emberbackup text-only backups remain
  compatible. Neither is encrypted; Drive sync is not yet integrated.
- Native local image Library: add from file/clipboard or reattach a saved
  image; bounded metadata-free JPEG/PNG, deduplicated SHA-256. A vision-capable
  local GGUF plus explicitly configured mmproj is required for actual image
  understanding; text-only models never pretend they saw an image.
- Text chat: the same local LLM can be used without a microphone. Current session is held in memory, not silently uploaded. Long voice replies are segmented on natural punctuation instead of dropping text after an arbitrary 700-character cutoff.
- Efficient native UI: GPU-rendered Qt Quick, subtle animations, dark colors, original procedural voice stage, keyboard-controlled composer, named audio-device and female-speaker selectors, local last-stage timing readouts. No runtime web UI.
- Replaceable intelligence: independently choose the voice backend (fast CPU Kokoro or higher-end local TTS API), the speaker and the local reasoner. Benchmark each component; model weight blending is a separate research path, not an assumed feature.
- Paired private RU/EN ASR measurements: `tools/compare_asr_engines.py` benchmarks user-installed Whisper.cpp and Qwen3-ASR on identical local 16 kHz corpus segments and writes only numeric error/latency summaries, with no copied recordings or transcripts. Run it manually when models are installed; see [voice quality lab](docs/VOICE-QUALITY-LAB.md).
- Portable storage under `D:\Games\Ember` by default, including `data/ember.ini`, per-session cache, engines and GGUF/ONNX weights. The actual install location is user-selected.
- Permission boundaries for future local files, programs, Internet and GitHub/Drive automation. No universal implicit access is granted by a text prompt. All current AI HTTP traffic is direct loopback with system proxies and redirects disabled.

## Current behavior

The backend launches a local `llama-server` bound to `127.0.0.1:43821` with one GGUF model and a fixed `ember` API alias. It posts current-session messages to the local `/v1/chat/completions` endpoint, never a remote URL. Audio recording runs only on explicit command, with a quota instead of a forced 30-second cutoff. On a microphone unplug, capture-device error, deliberate input switch, or safety-quota exhaustion during dictation, the app attempts to recover only a frame-aligned, disk-integrity-checked recorded prefix into the editable RU/EN composer and warns that its ending may be missing. Partial voice commands and failed disk writes are never submitted automatically. The current system is push-to-talk, with first-sentence TTS streaming when supported, not full-duplex. A compatible local TTS server, when selected, must already be running on `127.0.0.1:43822`; requests cannot be redirected to remote addresses. The caller must supply compatible engine binaries and licensed models; common stereo and 44.1/48 kHz microphones are converted to 16 kHz mono with a bounded resampler, while real hardware/device selection still needs acceptance testing. The built-in Kokoro path currently targets the **English v0.19 voice pack**. CLI Whisper transcription and Kokoro synthesis have bounded per-segment watchdogs (180 seconds and 120 seconds respectively); unconsumed native engine diagnostics are discarded in memory, not archived. For stereo capture devices with strongly opposite-polarity microphones, the resampler conservatively selects a usable channel instead of cancelling speech. These are resilience measures, not hardware-validated audio quality improvements. Russian speech output and multilingual voice quality are future acceptance items, not implied by the English model.

### Build

Requires MSVC x64, CMake 3.21+ and dynamically linked Qt 6.5+ with Quick, QuickControls2, Multimedia and Widgets.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
windeployqt --qmldir qml --release build\Release\Ember.exe
```

Copy the deployed executable and Qt DLLs into `D:\Games\Ember`; place external engine binaries under `engines` and separately downloaded models under `models` following [SETUP.md](docs/SETUP.md). Then open **Settings**, verify all paths, start the local brain, and test the selected voice.

### Custom EmberMind / Ember Voice research (no trained weights yet)

The intended product is **one user-facing assistant**, not a menu of unrelated
models. A private custom model path is documented in
[EmberMind and Ember Voice — custom model lab](docs/CUSTOM-MODEL-LAB.md):
curated legal EN/RU dialogue, coding, debugging and story examples; QLoRA/SFT
experiments on compatible Qwen3.5 checkpoints; carefully tested merging of
compatible adapters; and separately trained/conditioned English female TTS.
The local-only `tools/ember_mind_studio.py` validates training examples and
creates grouped train/eval JSONL without any network access or model downloads.
`tools/ember_mind_qlora.py` can now generate a conservative *offline* Axolotl
Qwen3.5-4B QLoRA recipe from pinned, already present Hugging Face weights and
datasets, and has an **explicit, separate one-shot train command** for a
user-provided CUDA/Axolotl environment. `tools/ember_voice_corpus.py`
validates consent-declared 24 kHz English single-speaker recordings and
prepares the official Qwen3-TTS Base raw fine-tuning JSONL format. No
training is launched from the application, and CI uses synthetic data only.
**Neither custom EmberMind nor Ember Voice has trained or verified weights.**
Never check private datasets into Git or treat synthetic CI as proof of
intelligence, speech or model quality.

## Manual local Qwen3.5 runtime acceptance (experimental)

A new standard-library-only, foreground probe checks **real** local
Qwen3.5 `llama-server` thinking/nonthinking stream behavior for English,
Russian, general reasoning and precise coding. It verifies a Windows
listener PID and rejects exposed private reasoning; only latency/counts
are reported. Run `py -3 tools/brain_mode_acceptance.py --self-test` to
check its synthetic fixtures, or follow
[the exact Windows manual test instructions](docs/LOCAL-MODEL-ACCEPTANCE.md)
with an already running, user-started local model. The source self-test
has passed but **real GGUF/RTX 4070 acceptance has not been performed**.

## One-click native startup and offline complete-package assembly

The main native Qt header now exposes an explicit **Start Ember / Stop Ember**
control. It starts the locally installed LLM and, if Qwen3-TTS or Qwen3-ASR
was selected, its managed native speech engine from preinstalled GGUF files.
It does not schedule anything, automatically download a catalog model or
launch workers on Windows login. Independent expert controls remain under
Settings. Kokoro and Whisper run only when speech/recording is explicitly used.

The full offline Windows **assembly pipeline** lives at
[`release/build-complete-offline.ps1`](release/build-complete-offline.ps1).
It requires existing, independently reviewed LLM/ASR/TTS weights and Windows
runtimes, license evidence, a deployed native Qt application and local
Python/Inno Setup 6 on the **build computer**. One foreground invocation
assembles a complete payload, runs real local LLM + native English TTS
smoke probes, produces Inno Setup and ZIP64 distributions with SHA-256 and
per-user install/uninstall verification. Stable SemVer additionally requires
the separate reviewed 17-check release acceptance record. No models,
runtimes or release binaries are currently attached to the development
branch. The new pipeline has not yet been executed on a Windows PC.

## Development state and downloads

GitHub Actions compiles the native Qt app and **tests** a versioned Inno Setup installer with silent installation, real QML startup and uninstall. Developer build files stay inside CI and are **not uploaded as previews**, by request. The public [Releases](https://github.com/ProhorovIlja/Ember/releases) page is reserved for accepted final installers with checksums, detailed notes, tested model inventory and GitHub source archives. The separate `.github/workflows/stable-release.yml` enforces reviewed hardware/quality/security gates, matching SemVer and clean installer acceptance before publication.

References and licensing: [docs/RESEARCH.md](docs/RESEARCH.md). Technical architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The Ember app code is MIT-licensed; Qt, models, GGUF converters and engine binaries retain their own licenses.

### Local chat and transcription functionality under development

Ember now includes a private **SQLite text-only chat archive** at `data/ember-chats.sqlite`; each conversation has its own local ID and title, and previous chats can be reopened from the sidebar. The UI loads up to 300 recent messages per session while SQLite keeps the complete text archive. Audio recordings are transient and never inserted into chat history.
The **Conversation Memory** dialog also offers **Recall from other saved chats**:
explicitly disabled by default, it performs a bounded lexical search of
other saved SQLite conversations only when a new message is submitted,
and shares at most a couple of short, explicitly unverified reference
excerpts with the same local LLM. The current conversation is excluded,
Private Mode never performs cross-chat retrieval, and no index, embeddings,
background service, new automation or external request is started.
The archive is currently **plaintext**, not encrypted; there is no automatic Google Drive synchronization. Deleting a selected conversation is explicit, and the installer intentionally does not erase the SQLite database on uninstall.

The Chat composer provides **click-to-start dictation with Cancel, To draft and Transcribe & send** with offline Whisper recognition in English or Russian (auto-detect, English, Russian). Speech capture is disk-backed and resampled in bounded ~25-second jobs; there is no 30-second dictation time limit, but there is a safety limit of **1 GiB of temporary raw capture**. The transcript is inserted into the composer for review and is **not automatically sent**. If a later ASR chunk fails during a long dictation, already recognized segments are recovered into the editable composer with an explicit incomplete-transcription warning; voice requests are never automatically sent from incomplete recognition. Voice conversations, meanwhile, use natural **English output** even after Russian speech input; the application interface is English-only. Text mode works in English/Russian and includes Conversation, Story studio, Troubleshooting and Development profiles.

Local text generation now consumes OpenAI/llama.cpp `text/event-stream` responses incrementally, checks malformed or oversized events, updates the chat UI in bounded batches and preserves interrupted partial text. Model reasoning received as a separate `reasoning_content` field or as streamed `<think>…</think>` markup is excluded from displayed assistant replies, voice playback, and saved SQLite chat history. Split tag prefixes remain withheld until the next SSE fragment resolves them. The native incremental filter now retains only a short partial tag (at most seven characters) rather than accumulating the model's hidden reasoning or rescanning the full reply after every token. A `finish_reason: length` response is explicitly marked incomplete rather than silently treated as a finished story or answer. A visible, user-triggered **Continue answer** action now appears for that specific incomplete reply; it submits an explicit local continuation request while leaving any unsent composer draft intact, rather than silently restarting or auto-spending GPU time. This is token streaming for **text**, not duplex audio or a measured first-phoneme improvement. The initial LLM context is configured for 8,192 tokens and generation for up to 2,048 tokens when writing long stories; performance depends on the chosen weights, quantization and actual RTX 4070 measurements.

The visual direction is **AMOLED black, restrained electric-blue accents, an original blue-flame emblem, and native Qt Quick**. Basic Windows/QML smoke tests do not constitute pixel-level visual QA on the target GPU or at multiple DPI settings.

**Local server identity protection:** Managed llama.cpp and speech.cpp
connections require a Windows kernel TCP listener owned by the exact
process Ember launched, bound specifically to 127.0.0.1. A fake local
health response cannot by itself mark a model ready or receive private
text/audio. External compatible localhost TTS remains possible through
an explicit **Trust external local TTS** toggle, disabled by default;
such a server can see the text sent for speech. This is PID-based
defense-in-depth, not protection against fully privileged local malware.


### Interim offline text-first milestone

For an early usable local programming/troubleshooting/chat experience,
see [the manual offline setup](docs/TEXT-FIRST-MILESTONE.md). Development
and Troubleshooting now request 2,048 output tokens at lower temperature
than creative Story mode, and accept up to 8,192 characters of user code
or diagnostic logs. A compatible licensed GGUF and Windows llama-server
remain user-supplied; models/binaries are **not** distributed. The source
also includes a Windows PowerShell 5.1 text-only staging/check helper,
with no downloads, subscriptions or scheduled work. This interim
functionality does not relax the no-public-preview policy.


### Local files and future agent capabilities

The Chat composer has an explicit **Add text file** picker to inspect one
UTF-8 code/log/document file within an 8,192-character editable draft; there
is no automatic sending or local file execution. See
[local tools roadmap](docs/LOCAL-TOOLS-ROADMAP.md) for the security and
confirmation boundaries for planned repository search, GitHub, Google Drive
and Internet research. These connected actions are **not yet integrated**.


Chat now also supports **Find in folder**: one user-approved local
directory, literal read-only search, bounded paths/bytes/results and
source line numbers placed into the visible composer. No autonomous
repository scanning or hidden code execution is performed.


### Optional deeper local reasoning

Text Chat includes a deliberate **Deep · On/Off** control, disabled by default.
Deep uses a larger 4,096-token local output budget in text Chat and
explicitly requests `chat_template_kwargs.enable_thinking=true` for GGUF
models using a compatible Qwen3/llama.cpp chat template. Local research,
GitHub inspection and coding-tool requests now respect this same setting,
using smaller, bounded agent-specific budgets with Deep off and larger
budgets only with Deep on. Hidden reasoning is not shown or archived.
The feature can use substantially more GPU time and may leave less of the
default 8,192-token context available for a large pasted prompt. It does
not transform a small model into GPT-6 High or guarantee that non-Qwen
models accept the template setting. Voice generation remains short. This
option is separate from, and never consumes, ChatGPT cloud usage limits.


The Chat toolbar can also use **GitHub · Read** to inspect a selected
**public** GitHub repository, open issues/PRs and a small UTF-8 source file.
This is an explicit one-shot network read from `api.github.com`, with no
token, redirects, mutation or background requests. Review the untrusted
preview before inserting it into the offline LLM's editable prompt. Private
GitHub account access and Google Drive remain future gated integrations.


For private GitHub repositories, **GitHub · Read** also supports a manually
connected fine-grained GitHub PAT with read-only permissions. The token is
stored in Windows Credential Manager, never in portable chat/settings or
the repository, and is sent only on an explicitly selected **Connected**
GitHub read to the official HTTPS API. **Public** reads use no credentials.
The application has no GitHub write actions or automatic scans. This is not
an OAuth account sign-in; Google Drive authentication is not implemented.


The Chat toolbar now also has **Wikipedia** for one user-triggered search
of English or Russian Wikipedia, returning up to five source-linked,
plain-text article snippets to the existing manual review dialog. No
background research is initiated, and snippets are not substitutes for
full articles. Wikimedia connectivity is required; the LLM itself remains
fully local. See `docs/LOCAL-TOOLS-ROADMAP.md`.


**Reviewed multi-file coding:** The local project agent may stage up to three
small, coordinated source replacements in different existing files. A combined
source preview, verified snapshot of every original and separate approval are
required. Private backups precede per-file QSaveFile commits; partial failures
attempt rollback, but an abrupt power failure is not cross-file atomic.
Tests remain independently approved. This is a bounded capability, not
unrestricted autonomous programming.

**Offline installation self-check:** Settings → **Check offline setup** now
inspects the selected Windows x64 PE engine headers, the local GGUF v2/v3
magic and multilingual Whisper model file presence, and the selected input
microphone. It runs no engines, downloads nothing and never contacts the
Internet. The on-screen report explicitly distinguishes file readiness from
verified inference, ASR quality, CUDA dependencies and licensing; choose
models directly with Browse even if their directory is not writable.

**Text-first milestone (November 2026):** The app starts on the **Chat**
page. Full duplex live Voice is deferred, but its implementation is preserved
for future work and does not need to run. **Start dictation** records offline
Russian/English microphone audio without needing the text LLM. While recording,
select **Cancel** (discard audio), **To draft** (transcribe into the editable
composer with no send), or **Transcribe & send** (complete ASR then request the
managed local model). The completion choice, not the start button, controls
sending. Incomplete/recovered audio never automatically sends; failure or
cancellation keeps the existing draft. Recognition works through installed
multilingual whisper.cpp or optional speech.cpp/Qwen3-ASR; these engines and
weights are separate local installation requirements. Speech quality and
hardware still require testing. A PowerShell-only offline setup helper
`release/prepare-text-first.ps1` now accepts an optional local Whisper CLI
and multilingual GGML `.bin` in addition to llama-server/GGUF, with
`-RequireAsr` and hash-checked staging. It never downloads or runs a model.

**Wikipedia agent (on demand):** With the managed local model running, select
**Wiki agent** after typing a question. Ember asks before allowing up to three
model-selected English or Russian Wikipedia REST searches. Only official
HTTPS article-search endpoints are called (no arbitrary web browsing),
returning source-linked short snippets. The model produces a reviewable
answer; it cannot write files, browse other sites or perform background work.
The original manual **Wikipedia** button is unchanged. Real local GGUF tool
selection and regional HTTP reachability still need physical testing.

**Google Drive (manual read-only)**: The Chat toolbar provides Connect,
List folder, Read one text/Google Doc, and Disconnect operations. Desktop
OAuth uses the system browser, loopback localhost and PKCE, with the
refresh token in Windows Credential Manager. Supply a Google Cloud
**Desktop** OAuth client ID; grant the `drive.readonly` scope deliberately.
The old metadata-only credential is **not** reused: consent is required
again. The client shows up to 20 entries in a chosen folder, or reads
one explicitly selected UTF-8 text file / Google Doc (Google Docs
exported to plain text). Files are screened by type and size (96 KiB),
then shown in a review dialog with an excerpt limited to 12,000 characters.
This is not a Drive search, image/PDF/Sheets reader, agent tool, write
connection, scheduled sync or backup. No real account sign-in or target
Windows hardware acceptance is verified by CI alone. Broad Drive read
permissions may require appropriate Google consent-screen verification
or test-user setup.
