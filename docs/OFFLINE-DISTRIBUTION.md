# Ember — offline, country-resilient Windows distribution

## Acceptance contract

The final edition should be installable and usable for ordinary text, English speech and Russian/English press/release dictation on a Windows computer that has **no VPN, no zapret, no network, no preexisting model cache and no foreign service login**. Web research, Google Drive sync and online collaboration will necessarily require network access when the user opts into those features. Neither a verified Qt EXE nor a partial launcher is an accepted complete offline installation. All menus remain English.

The release requires a complete, versioned **source/provenance, license and SHA-256 inventory** of the native llama.cpp, whisper.cpp, speech.cpp or sherpa-onnx engines, required DLLs/codec/phonemization resources, all selected GGUF/ONNX weights, and fonts/assets. Some weights may not legally permit redistribution; check each upstream license and usage terms individually before adding the item to the public package. Asset source URLs in a manifest are evidence references and never download instructions.

## Source-first asset assembly (development tooling, no model weights shipped)

From an existing trusted local directory containing `engines/`, `models/` and `licenses/`:

```powershell
python tools/offline_bundle.py self-test
python tools/offline_bundle.py inventory --root D:\EmberInputs --manifest D:\offline-review.json
python tools/offline_bundle.py verify --root D:\EmberInputs --manifest D:\offline-review.json
```

Inventory output includes SHA-256 and sizes but deliberately marks **every item unapproved** with blank source/license evidence. A maintainer must investigate its upstream license, fill `source_url` (HTTPS), `license_path` (a matching manifest-listed `licenses/` file), its `component` and explicitly set `redistribution_approved=true` only where allowed. Approval flags are a human attestation, **not a license detector**.

Only after that explicit review:

```powershell
python tools/offline_bundle.py verify --root D:\EmberInputs --manifest D:\approved-offline.json --redistribution-required
python tools/offline_bundle.py stage --root D:\EmberInputs --manifest D:\approved-offline.json --output D:\EmberOfflineStaged
```

The staged folder contains only allowlisted `engines/`, `models/`, `licenses/` plus `offline-assets.json`; SHA-256 is checked before and after copying. It refuses to overwrite a destination, links/symlinks, traversal paths, Windows case-insensitive collisions, unlisted files and changed bytes. No Python packages, cloud service, curl or runtime download are used. Never include `data/`, chats, private audio, saved device IDs or tokens in a distributable asset folder.

The stable tagged workflow now **fails closed** if `build/Release/offline-assets.json` is absent or verification fails, and passes `-RequireOfflineBundle` to the Inno installer builder. A future licensed artifact staging step is still needed before this workflow can publish a complete release. The Inno Setup wrapper accepts `-RequireOfflineBundle` when building a release installer. It will refuse any payload folder without a fully approved and verifiable `offline-assets.json`. Ordinary internal development builds remain partial and intentionally publish no preview downloads. The complete, signed-off release needs an additional step to insert vetted staged files into the native deployed Qt output **before** packaging; this has not been implemented or claimed complete yet.

## Region access and manual alternatives

It is not possible to guarantee that GitHub, Hugging Face, model CDNs, mirrors or a chosen host will remain reachable from Russia. The final project should offer a legal alternate distribution channel and checksummed offline package, or transfer of the complete package via local removable media. Splitting large archives can help with unreliable downloads, but is not a workaround for license restrictions.

For components that cannot be redistributed, provide exact expected folder names and a manual local **Import** workflow with checksum validation, then disclose that the edition is not a one-download full package. For speech.cpp in particular, naming a model by catalog alias can invoke a download; a local .gguf file path avoids catalog fetching, whereas simply shipping `speech.exe` does not. Ember now uses local `models/speech/<alias>.gguf` files for the four supported Qwen3-TTS/Qwen3-ASR aliases and **blocks catalog downloads by default**. The only bypass is an explicit Settings toggle. The HTTP requests omit the catalog model ID when local file-backed TTS/ASR is selected, allowing speech.cpp to select the single loaded task's model. This integration needs real engine/hardware verification. Upstream speech.cpp accepts GGUF file paths directly (https://github.com/nyosegawa/speech.cpp/blob/main/docs/models.md). Its default Windows catalog cache is `%LOCALAPPDATA%\speech.cpp\models`, overridable with `SPEECH_MODEL_DIR`. Offline model startup must be integrated and physically tested, not assumed because some files happen to be present.

## Still missing before final release

Real Windows 10 / RTX 4070 test with airplane/network disconnect, clean installer, full ASR/TTS/LLM and warm/cold voice benches; human-graded English female voices, RU/EN WER, GPU coexistence, asset code-signing/provenance, full offline weights with legal approval, usable import UI, alternate distribution endpoint and final installer size review. Optional Internet tools and Drive backups must stay independent from the offline core.
