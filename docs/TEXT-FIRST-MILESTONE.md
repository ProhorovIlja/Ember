# Offline text-first setup (Windows 10)

**Purpose:** get a local text chat **plus RU/EN dictation** for software development, troubleshooting,
story writing and ordinary conversation working before the separate premium
voice/visual features are finished. This is NOT a public preview or release.

The current C++/Qt app builds and has a local llama.cpp HTTP client, but does
not ship a third-party server executable or licensed GGUF weights. No
cloud model, login, VPN, network fetch or speech model is required for typed
chat. Dictation adds one offline multilingual ASR engine/model but no TTS or
spoken conversation runtime. Recordings have explicit Cancel / To draft /
Transcribe & send completion actions; speech models for TTS are not required. A suitable already-downloaded llama-server and GGUF must still be
supplied by the user or bundled later with redistribution rights.

## Prepare a deployed Qt app

Make sure the deployed application folder already contains Ember.exe and
Qt6Core.dll. With Windows PowerShell 5.1 or later, run from the repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\release\prepare-text-first.ps1 `
  -AppDirectory 'D:\Games\Ember' `
  -LlamaServer 'C:\Downloaded\llama\llama-server.exe' `
  -ModelGguf 'C:\Downloaded\models\licensed.gguf' `
  -WhisperCli 'C:\Downloaded\whisper\whisper-cli.exe' `
  -WhisperModel 'C:\Downloaded\models\ggml-base.bin' `
  -RequireAsr
```

The helper copies the local engine EXE and sibling DLLs and the GGUF into
`engines/llama/llama-server.exe` and `models/llm/model.gguf` under the deployed
app. When both optional ASR paths are supplied, it also copies the offline
whisper-cli binary and its DLLs into `engines/whisper/` and a multilingual
GGML .bin into `models/stt/ggml-base.bin`. The `-RequireAsr` switch
refuses text-only staging and `-CheckOnly -RequireAsr` validates presence.
Whisper model compatibility is checked only by the real engine, not this
file copier. It checks EXE/GGUF file signatures, verifies SHA-256 after copy,
refuses replacing different existing files, cleans newly staged files on
failure, and never downloads, executes, schedules or uploads anything.
Run again with `-AppDirectory 'D:\Games\Ember' -CheckOnly -RequireAsr` to
check presence after staging. Python is not required on the user's PC.

Then open Ember.exe, select **Start brain**, wait for local health readiness,
choose **Troubleshooting** or **Development**, and send a test prompt. Text
conversation is independent of ASR, TTS or an Internet connection.

## Native, read-only offline setup check

In Ember Settings, click **Check offline setup** to see a local-only file
preflight for llama-server, GGUF v2/v3, multilingual Whisper binary and
Windows microphone detection. The check validates a native x64 PE signature,
GGUF format header, reasonable file size and rejects English-only `.en.bin`
for the RU/EN dictation milestone. It never executes the engine, reads
entire model weights, talks to an external host, or downloads packages.
A green preflight only means the selected paths look plausible; a real
RTX 4070 inference session, correct CUDA dependencies, model checksum,
license evidence and RU/EN microphone acceptance remain required.

## What is and is not verified

The new text-first profiles request up to 2048 completion tokens for coding
and diagnosis, with lower temperature than Story. Composer and model request
accept up to 8192 characters of pasted code or logs. The llama.cpp server
retains its default context of 8192 **tokens**; token-dense source material
can still overflow the actual available context, which is model-dependent.
There is no proven guarantee that an arbitrary 8192-character code paste fits.

Staging checks **cannot** prove that the supplied GGUF model is supported
by that llama-server version, that the GPU/CUDA DLL stack is complete, or
that a particular model reasons accurately. The target Windows 10 computer
must still run a real offline model and pass Development, Troubleshooting,
Conversation and Story examples. Verify cold/warm TTFT, GPU memory, real
code changes, log analysis, interruption, SQLite persistence and restarting
a session. Only then call the private text-first milestone usable.

Do not publish third-party executables or model weights as a public installer
until their exact version, sources and licenses permit redistribution.
No public Release is authorized by this milestone.


## Manually verify the real local GGUF tool-calling handshake

After installing a trusted CUDA-capable `llama-server.exe` and a compatible instruct GGUF, start the local brain in Ember. The managed `llama-server` is launched with `--jinja` to expose OpenAI-compatible tool calls. The local model's precise chat template can still affect whether tool calling works; compilation/Qt tests cannot establish this. From the repository, with Python available to the **developer only** (not required by the Ember user interface), run:

```powershell
python tools/text_benchmark.py --self-test
python tools/text_benchmark.py --tool-verify
```

The second command deliberately calls **only** `http://127.0.0.1:43821/v1/chat/completions` on the already-running local engine. It performs a fully synthetic four-step function-calling handshake (`project_map` → `search_text` → `read_source` → `propose_patch`) and then a final answer. Each tool result is a fixture string; **no function is executed, no source file changed, no filesystem browsing, no GitHub or Drive API called and no model contents or prompts saved**. The tool reports only correctness/latency per step. A failure indicates incompatible tool call formatting or inadequate instruction following with this GGUF/template; a pass proves only the structured protocol, not reliable autonomous programming. The script is an explicit user-invoked test, not an automatic background monitor. `--output D:\\EmberBench\\tools.json` optionally saves these metrics.
