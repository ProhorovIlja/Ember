# Ember — offline, country-resilient Windows distribution

**2026-11-22 structural approval gate:** Approved bundles must contain Ember's exact native text-first default paths: `engines/llama/llama-server.exe` (LLM runtime), `models/llm/model.gguf` (GGUF LLM), `engines/whisper/whisper-cli.exe` (ASR runtime), and `models/stt/ggml-base.bin` (multilingual GGML ASR weights). The matching components must be `llm` or `asr`, not merely arbitrary placeholder labels. For every packaged Windows executable/DLL under `engines/`, the verifier checks MZ/PE/AMD64; for all `models/*.gguf` it checks GGUF v2/v3 and a 1-MiB minimum; the default Whisper model must start with original GGML `lmgg` magic and meet the same size minimum. Synthetic CI assets carry valid *headers* but are intentionally not executable or inference-capable. The test includes rehashed forgery attempts and renamed-file attacks. SHA-256 detects tampering relative to a trusted manifest, not self-consistent forgery. This is only structural preflight: binaries, licenses, CUDA dependencies, actual GGUF tensor contents, multilingual ability, and offline end-to-end inference still require manual validation on the target Windows machine.

## Acceptance contract

The first complete edition should be installable and usable for ordinary text chat, local development and Russian/English record/stop dictation on a Windows computer that has **no VPN, no zapret, no network, no preexisting model cache and no foreign service login**. Web research, Google Drive sync and online collaboration will necessarily require network access when the user opts into those features. Neither a verified Qt EXE nor a partial launcher is an accepted complete offline installation. All menus remain English.

The text-first release requires a complete, versioned **source/provenance, license and SHA-256 inventory** of its native llama.cpp and multilingual ASR engines, runtime DLLs, selected model weights, and fonts/assets. Optional future voice bundles must independently inventory their speech.cpp/sherpa-onnx engines, phonemization resources, models and licenses. Some weights may not legally permit redistribution; check each upstream license and usage terms individually before adding the item to the public package. Asset source URLs in a manifest are evidence references and never download instructions.

## Source-first asset assembly (development tooling, no model weights shipped)

From an existing trusted local directory containing `engines/`, `models/` and `licenses/`:

```powershell
python tools/offline_bundle.py self-test
python tools/offline_bundle.py inventory --root D:\EmberInputs --manifest D:\offline-review.json
python tools/offline_bundle.py verify --root D:\EmberInputs --manifest D:\offline-review.json
```

Inventory output includes SHA-256 and sizes but deliberately marks **every item unapproved** with blank source/license evidence. A maintainer must investigate its upstream license, fill `source_url` (HTTPS), `license_path` (a matching manifest-listed `licenses/` file), its `component` and explicitly set `redistribution_approved=true` only where allowed. Approval flags are a human attestation, **not a license detector**. A verified approved **text-first** package must contain at least one LLM and multilingual ASR model plus corresponding native engine components (a shared engine may serve more than one task). TTS weights are optional for this milestone, but including them requires a compatible approved TTS engine. The final spoken-conversation milestone retains separate voice-quality acceptance. This is a structural minimum only; speech quality, working codecs, CUDA/Vulkan DLLs and all licenses still require separate acceptance.

Only after that explicit review:

```powershell
python tools/offline_bundle.py verify --root D:\EmberInputs --manifest D:\approved-offline.json --redistribution-required
python tools/offline_bundle.py stage --root D:\EmberInputs --manifest D:\approved-offline.json --output D:\EmberOfflineStaged
```

The next step merges a clean Windows Qt deployment and reviewed offline assets, then builds a ZIP64 portable archive without redownloading anything:

```powershell
python tools/offline_bundle.py assemble --root D:\EmberOfflineStaged --manifest D:\EmberOfflineStaged\offline-assets.json --application build\Release --output D:\EmberComplete
python tools/portable_zip.py --root D:\EmberComplete --output D:\EmberComplete-Windows.zip
pwsh release/build-dev-installer.ps1 -PayloadDirectory D:\EmberComplete -OutputDirectory dist -RequireOfflineBundle -VerifyInstall
```

The assembler refuses to mix user `data/` chats or `cache/` audio into the installer. It also refuses untracked engine/model files in the Qt payload. ZIP64 supports multi-gigabyte GGUF files; already-compressed weights are stored without costly recompression, and each source asset is verified with SHA-256. This is a working packaging pathway using **locally supplied assets only**, not an official prepopulated Release. The staged folder contains only allowlisted `engines/`, `models/`, `licenses/` plus `offline-assets.json`; SHA-256 is checked before and after copying. It refuses to overwrite a destination, links/symlinks, traversal paths, Windows case-insensitive collisions, unlisted files and changed bytes. No Python packages, cloud service, curl or runtime download are used. Never include `data/`, chats, private audio, saved device IDs or tokens in a distributable asset folder.

The stable tagged workflow now **fails closed** if `build/Release/offline-assets.json` is absent or verification fails, and passes `-RequireOfflineBundle` to the Inno installer builder. A future licensed artifact staging step is still needed before this workflow can publish a complete release. The Inno Setup wrapper accepts `-RequireOfflineBundle` when building a release installer. It will refuse any payload folder without a fully approved and verifiable `offline-assets.json`. Ordinary internal development builds remain partial and intentionally publish no preview downloads. The complete, signed-off release needs an additional step to insert vetted staged files into the native deployed Qt output **before** packaging; this has not been implemented or claimed complete yet.

## Explicit on-demand real-model acceptance (not a preview)

When the complete approved bundle exists on the actual Windows machine, the maintainer may manually run a synchronous acceptance probe. This uses Python from the development checkout; Python is NOT a dependency of the final installed Ember application.

```powershell
python tools/offline_acceptance.py --self-test
python tools/offline_acceptance.py --root D:\EmberComplete
python tools/offline_acceptance.py --root D:\EmberComplete --run --gpu-layers 99 --context 8192
python tools/offline_acceptance.py --root D:\EmberComplete --run --gpu-layers 99 --en-wav D:\Samples\english.wav --en-reference D:\Samples\english.txt --ru-wav D:\Samples\russian.wav --ru-reference D:\Samples\russian.txt --report D:\Ember-QA-metrics.json
```

The default invocation only rechecks the complete manifest and clean native app files, without launching anything. Only explicit --run starts the approved llama-server EXE using the same local 127.0.0.1:43821 /health and streaming /v1/chat/completions transport as the Ember app. Windows netstat is used to refuse an occupied or foreign-owned port; a mismatched listener must never receive prompts. The managed server exits when the test concludes. Tests sequentially check a real model's streaming SSE and an exact simple instruction; optional EN/RU *user-provided* 16-kHz mono PCM16 WAV + reference text are transcribed by the actual bundled whisper-cli and scored with WER/CER. Native executable stdout/stderr are discarded, recognition scratch is deleted, and output/reports contain metrics only. This is **not** a security sandbox; executable dependencies or subprocesses may perform networking or other side effects. Do not run untrusted engine binaries.

The report never sets release_approved=true. Passing means only these particular on-demand probes succeeded with the supplied model and samples; a clean Windows 10 RTX 4070 field test, extended Russian/English dictation, real UI controls, CUDA/DLL deployment, adversarial model tests, privacy and license validation are still required. No downloads, background monitoring, scheduled tasks, registry changes, release publication or automatic reruns occur.

## Region access and manual alternatives

It is not possible to guarantee that GitHub, Hugging Face, model CDNs, mirrors or a chosen host will remain reachable from Russia. The final project should offer a legal alternate distribution channel and checksummed offline package, or transfer of the complete package via local removable media. Splitting large archives can help with unreliable downloads, but is not a workaround for license restrictions.

For components that cannot be redistributed, provide exact expected folder names and a manual local **Import** workflow with checksum validation, then disclose that the edition is not a one-download full package. For speech.cpp in particular, naming a model by catalog alias can invoke a download; a local .gguf file path avoids catalog fetching, whereas simply shipping `speech.exe` does not. Ember now uses local `models/speech/<alias>.gguf` files for the four supported Qwen3-TTS/Qwen3-ASR aliases and **blocks catalog downloads by default**. The only bypass is an explicit Settings toggle. The HTTP requests omit the catalog model ID when local file-backed TTS/ASR is selected, allowing speech.cpp to select the single loaded task's model. This integration needs real engine/hardware verification. Upstream speech.cpp accepts GGUF file paths directly (https://github.com/nyosegawa/speech.cpp/blob/main/docs/models.md). Its default Windows catalog cache is `%LOCALAPPDATA%\speech.cpp\models`, overridable with `SPEECH_MODEL_DIR`. Offline model startup must be integrated and physically tested, not assumed because some files happen to be present.

## Still missing before final release

Real Windows 10 / RTX 4070 test with network disconnected, clean installer, complete LLM and RU/EN dictation, cancellation/draft/send fidelity, local coding tools and long-session stability; RU/EN WER, VRAM and latency, legal model/engine inventories, complete offline weights, local import UI, suitable distribution channels and installer size review. English TTS naturalness, spoken latency and full-duplex barge-in belong to a **later voice milestone**, not a text-first release blocker. Optional Internet tools and Drive backups must stay independent from the offline core.

The installer builder accepts either repository-relative CI paths or absolute
`-PayloadDirectory` / `-OutputDirectory` paths, so a large offline payload can
reside on a separate storage drive without being copied into the Git repository.

The `-VerifyInstall` option works both on GitHub Actions and on a normal
Windows PC: it uses an isolated, uniquely named temporary installation
directory without requiring the GitHub-specific `RUNNER_TEMP` variable.
The uninstaller is expected to leave any user-created private data untouched.
\n**2026-11-22 isolated smoke profiles and clean builds:** The native `--smoke-test` now creates its SQLite, settings, Image Library and microphone scratch under a disposable OS temporary directory, not under the release/installed application's `data/` or `cache/`. Optional QML smoke diagnostics go only to a caller-provided `EMBER_SMOKE_REPORT_PATH` outside the app and to stdout. Production and portable user paths are unchanged. Both native Windows CI and the Inno installer smoke check explicitly reject newly created app-local data/cache/reports. The installer build wrapper refuses a contaminated source payload, and the ZIP64 tool rejects such data and common secret file types rather than archiving an entire previously launched app. This is a privacy and packaging control, not proof that a model works offline.\n
**Recursive packaging safety:** Both manual installer and ZIP64 assembly now examine all candidate files, not only top-level folders. Secret-like files (SQLite, .env, credentials.json, private keys, diagnostic output), links and Windows junctions cause refusal rather than inclusion. The Inno wrapper no longer invokes Chocolatey to fetch a missing compiler; a trusted preinstalled Inno Setup 6 is required for offline builds. These guards do not independently prove that an arbitrary file has no private text within it; review the payload inventory before distribution.

**2026-11-22 real local model token inspection:** The explicitly invoked Windows `python tools/offline_acceptance.py --root ... --run` now checks fixed EN/RU diagnostic prompts against the *actual local GGUF's* `/apply-template` and `/tokenize` APIs after its ordinary streamed instruction check. It records model-specific prompt token counts, active context and reserved reply tokens without retaining generated text or real conversation history. All requests go to PID-verified `127.0.0.1:43821` with redirects/proxies disabled; malformed token IDs, missing JSON, unexpectedly large responses, changed socket owners and a sample context overrun fail explicitly. The default invocation remains read-only with no model launch, and the CI self-test uses only local response-shape fixtures. This helps measure the new heuristic context guard but cannot guarantee all arbitrary user prompts fit. Upstream documentation: https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md .\n

## Manual local GGUF selection without duplicate files or VPN

Native Settings now offer separate **Choose file** / **Clear** controls for the
current Qwen3-TTS 0.6B or 1.7B voice and the current Qwen3-ASR 0.6B or 1.7B
recognizer. A user who already obtained an approved compatible speech.cpp GGUF
can point Ember directly at an existing local drive file. It saves only the
absolute file path under the corresponding model alias in local QSettings:
there is **no upload, network request or multi-gigabyte second copy**. Users
can select a different GGUF for every supported TTS/ASR alias and switch back
without moving the weights. Clear removes the manual override and returns to
the original bundled `models/speech/<alias>.gguf` fallback.

Startup checks the pinned file **again**, rejecting moved, missing, linked,
network-share, tiny and invalid-version GGUF files. An invalid pinned file
fails closed rather than silently invoking catalog downloads, even if that
setting had been explicitly enabled. The launch uses the file path as a
QProcess argument, not shell text. Default catalog downloading remains blocked.
Structural GGUF checking **does not** verify actual model family, tensor
completeness, license, or that a Qwen3 model runs with the installed exact
speech.cpp release. Genuine offline launch/listening tests on Windows 10,
including RTX 4070 with Vulkan and English speech/RU-EN recognition, remain
required before a release claim. See upstream:
https://github.com/nyosegawa/speech.cpp/blob/main/docs/models.md

Windows manual GGUF selection additionally checks the actual drive type using
GetDriveTypeW, rejecting UNC and mapped network drives. Fixed, removable,
optical and local RAM disks are allowed; unknown or remote types fail closed.
This is a filesystem-location preflight, **not** a general guarantee that an
installed third-party native executable never performs networking or that a
volume cannot change after selection. Confirm offline behavior on actual
Windows hardware. The Settings native speech card now sizes from its Qt
ColumnLayout content, rather than relying on a hard-coded height that could
clip the two model pickers at different display scales.

The existing Settings **Check offline setup** now includes a read-only native
speech.cpp summary whenever Qwen3-TTS or Qwen3-ASR is selected. It distinguishes
a bundled GGUF, a manually selected valid-header GGUF, a missing or invalid
manual override (which always blocks), and catalog-only operation that would
require explicit opt-in/network access. It also identifies a missing
speech.exe. This inspection does not start a model, download anything, save
private prompts or assert successful real inference.


## Full first stable release voice acceptance (not an internal text-first preview)

The publicly downloadable first Ember stable Release cannot be approved on
text/ASR CI alone. The release gate also requires reviewed real Windows 10
English-voice listening quality, native audio output on target hardware,
interrupt/cancel cleanup, licensed complete offline TTS weights/engines and a
genuine no-VPN clean-install test. These are distinct manual evidence records
in release/verify_release.py; neither a model header nor a fake local server
is enough. The text-first preparation tools remain useful for internal
development and must not be advertised as the complete public edition.


## Disable speech.cpp's independent version-check network path

Upstream speech.cpp documents a once-daily command-line update notice:
https://github.com/nyosegawa/speech.cpp/blob/main/docs/install.md
This check is **separate** from GGUF catalog/model downloading. Ember's native
QProcess environment now forces `SPEECH_NO_UPDATE_CHECK=1` on every
user-started managed speech process, regardless of whether the user separately
enabled catalog downloads. A Qt regression verifies the inherited environment
cannot override the opt-out and that other process variables are preserved.

This removes the documented upstream *version-check* request path; it is
not a proof that every unknown native binary cannot initiate arbitrary
network activity. Run a genuine fresh-install disconnected-network test with
trusted/pinned speech.cpp binaries and explicit GGUF paths before any public
release, and ensure no unexpected process-level network access occurs.


## Pinned official Windows speech.cpp runtime, offline verification

Official publisher source: https://github.com/nyosegawa/speech.cpp
Pinned release: https://github.com/nyosegawa/speech.cpp/releases/tag/v0.11.2
Windows Vulkan archive:
https://github.com/nyosegawa/speech.cpp/releases/download/v0.11.2/speech-0.11.2-windows-x64-vulkan.zip

The actual GitHub release asset metadata reports:
- `speech-0.11.2-windows-x64-vulkan.zip`
- **29,726,411 bytes**
- SHA-256 **`2cf46f0351a2a90b9ce0bfb83cf63ac209ca31c88df9a5bc0126898a20a58586`**

The original speech.cpp repository currently carries the MIT license, but
all its **binary dependencies, GGUF model conversions and original weights**
still need their own pinned license/NOTICE and redistribution review.

The new standard-library-only helper
`tools/verify_speechcpp_archive.py` supports a legal, manual
**download-once / USB-transfer** workflow without any model registry,
installation script, network access or executable launch:

```powershell
python tools/verify_speechcpp_archive.py --self-test
python tools/verify_speechcpp_archive.py --archive D:\Downloads\speech-0.11.2-windows-x64-vulkan.zip
python tools/verify_speechcpp_archive.py --archive D:\Downloads\speech-0.11.2-windows-x64-vulkan.zip --output D:\EmberInputs\engines\speech
```

Create `D:\EmberInputs\engines` beforehand; the `speech` directory must
**not** exist yet. The script checks exact official ZIP size and SHA-256,
normalizes internal member paths, refuses symlinks, Windows traversal,
reserved/colliding names, excessive extraction and an absent/non-x64 root
`speech.exe`. Only after verification does it stage to a new directory,
without overwriting a previous runtime. Manual model setup remains separate
and requires genuine Qwen3 speech GGUF files, verified licensing and real
Windows execution to confirm model compatibility.

The repository/CI self-test generates tiny synthetic ZIPs and never downloads
or runs the official binary, so it **cannot** assert that the official asset
has already been staged or that its dependency DLLs load on a clean Windows
system. For portability in Russia, obtain the lawfully redistributable ZIP
through an available channel once, check the SHA-256 on the target PC and
transfer it via removable media if needed; no particular remote host is
guaranteed accessible without a VPN. Never silently download models on first
run. Native Ember still requires a complete approved offline manifest before
public release.


### Full-voice inventory review template (zero approved files)

`release/offline-voice-assets.template.json` is a **deliberately
unapproved** sample inventory of native llama.cpp, multilingual whisper.cpp,
Windows speech.cpp, a Qwen3-TTS GGUF alias Ember can start, and license
evidence. Every size is zero, every SHA is blank, and every redistribution
flag is `false`. This is not a download list, assembled distribution or
an assertion that files/licenses were obtained. A maintainer replaces each
placeholder with the exact verified local artifact size/SHA/license path and
explicitly approves it only after reviewing actual rights, transitive DLLs,
genuine inference and sound quality. Existing
`release/offline-assets.template.json` remains the internal text-first
milestone template. The stable `--require-voice` check validates a real
approved manifest and refuses either template unmodified.


### One-shot native speech inference acceptance (Windows, on demand only)

A complete, reviewed offline bundle must do more than contain a PE header and
a plausible GGUF header. After obtaining the real licensed assets and staging
them in one verified folder, run the following manually on the target Windows
PC, ideally with its external network disconnected:

```powershell
python tools/native_speech_acceptance.py --self-test
python tools/native_speech_acceptance.py --root D:\EmberComplete
python tools/native_speech_acceptance.py --root D:\EmberComplete --run --model qwen3-tts-0.6b --device cpu --samples 2 --report D:\ember-speech-cpu.json
python tools/native_speech_acceptance.py --root D:\EmberComplete --run --model qwen3-tts-0.6b --device gpu --samples 2 --report D:\ember-speech-gpu.json
```

The default command verifies an **approved** complete voice manifest, content
hashes and real GGUF header but launches nothing. Only explicit `--run` starts
`engines/speech/speech.exe` with the actual bundled TTS GGUF, the same loopback
host/port and CPU/GPU flags as Ember, and a bounded model-load timeout.
The test rejects occupied or foreign-owned ports and checks the PID before
and after private HTTP probes, disables speech.cpp's update check, sends fixed
non-private English sample sentences and validates returned PCM16 audio.
Each trial measures first HTTP audio byte, PCM duration and signal quality;
the process is stopped in a `finally` block. Output reports contain only
numeric measurements and model/voice labels, never WAV recordings or user
chat text, and cannot silently overwrite an existing result.

Only the 0.6B or 1.7B TTS model **actually listed in the approved manifest**
can pass. The test does not download a model or approve redistribution.
Passing it is not proof of enjoyable English voice, real audio-device
playback, microphone accuracy, multi-process RTX 4070 VRAM sufficiency,
external network isolation of a third-party executable, or a finished app.
Human blind listening and clean Windows 10 acceptance remain mandatory.


### Native worker trust and readiness, 22 November 2026

The managed `llama-server` and `speech.cpp` processes are now marked Ready
only if all conditions hold: the exact Windows loopback listener still
belongs to the launched process ID; an HTTP 200 `/health` response contains
no more than 4096 bytes; its media type is `application/json`; and the parsed
JSON object contains the literal string `"status":"ok"`. An HTTP 200 with
HTML, malformed JSON or a loading/error status never means Ready. This
matches the documented `/health` protocol of both upstream projects.

Native llama.cpp, whisper.cpp, sherpa-onnx Kokoro, speech.cpp and the explicit
local project test subprocesses now receive an environment with inherited
credential-like variables removed (API keys, tokens, secrets, passwords and
cloud-provider credential prefixes). Windows PATH, SystemRoot and GPU/Vulkan
driver settings are preserved. The one-shot Python offline and comparison
tools use the same credential-removal policy and additionally set
`SPEECH_NO_UPDATE_CHECK=1`, `HF_HUB_OFFLINE=1` and
`TRANSFORMERS_OFFLINE=1`.

These controls reduce accidental exposure to native third-party executables.
They do **not** authenticate downloaded EXE/DLLs, prove GGUF weight integrity,
block every possible outbound socket, isolate the process at the OS level,
secure untrusted arbitrary repositories or replace a disconnected-PC
acceptance test. Opt-in model catalog downloads in the native UI remain
independent of the strict one-shot Python offline acceptance scripts.
