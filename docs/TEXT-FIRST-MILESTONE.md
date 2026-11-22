# Offline text-first setup (Windows 10)

**Purpose:** get a local typed chat for software development, troubleshooting,
story writing and ordinary conversation working before the separate premium
voice/visual features are finished. This is NOT a public preview or release.

The current C++/Qt app builds and has a local llama.cpp HTTP client, but does
not ship a third-party server executable or licensed GGUF weights. No
cloud model, login, VPN, network fetch or speech model is required for typed
chat. A suitable already-downloaded llama-server and GGUF must still be
supplied by the user or bundled later with redistribution rights.

## Prepare a deployed Qt app

Make sure the deployed application folder already contains Ember.exe and
Qt6Core.dll. With Windows PowerShell 5.1 or later, run from the repository:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\release\prepare-text-first.ps1 `
  -AppDirectory 'D:\Games\Ember' `
  -LlamaServer 'C:\Downloaded\llama\llama-server.exe' `
  -ModelGguf 'C:\Downloaded\models\licensed.gguf'
```

The helper copies the local engine EXE and sibling DLLs and the GGUF into
`engines/llama/llama-server.exe` and `models/llm/model.gguf` under the deployed
app. It checks EXE/GGUF file signatures, verifies SHA-256 after copy,
refuses replacing different existing files, cleans newly staged files on
failure, and never downloads, executes, schedules or uploads anything.
Run again with `-AppDirectory 'D:\Games\Ember' -CheckOnly` to
check presence after staging. Python is not required on the user's PC.

Then open Ember.exe, select **Start brain**, wait for local health readiness,
choose **Troubleshooting** or **Development**, and send a test prompt. Text
conversation is independent of ASR, TTS or an Internet connection.

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
