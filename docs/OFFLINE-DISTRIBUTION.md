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
