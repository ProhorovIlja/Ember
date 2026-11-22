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
The copier now rejects obvious file-format mismatches before staging:
Windows PE must identify native AMD64 (not x86); GGUF must be v2 or v3 and
at least 1 MiB; Whisper models must have whisper.cpp's original GGML magic,
be at least 1 MiB and not use the English-only `.en.bin` filename.
A valid header is not proof of complete weights, tokenizer compatibility,
multilingual capacity or good recognition; only real offline inference can
establish those. SHA-256 is verified after each copy; different existing
user files are never overwritten. A failed stage removes newly copied files.
No downloads, executables, scheduled tasks or uploads are invoked.
Run again with `-AppDirectory 'D:\Games\Ember' -CheckOnly -RequireAsr` to
check presence after staging. Python is not required on the user's PC.

Then open Ember.exe, select **Start brain**, wait for local health readiness,
choose **Troubleshooting** or **Development**, and send a test prompt. Text
conversation is independent of ASR, TTS or an Internet connection.

## Local LLM resource and startup controls

Settings now permits explicit context windows of 4,096 / 8,192 / 16,384 /
32,768 tokens and GPU layer offload 0 / 20 / 50 / 99 for the managed
llama-server. Text responses may also use Auto/profile defaults or an
explicit 2,048 / 4,096 / 8,192-token output budget. This is a maximum
generation budget, not a guarantee of answer length: it is capped at half
the configured context window (and 8,192 tokens overall) to reserve room
for prompts/history. Spoken replies retain a strict 512-token cap. Large
budgets increase generation time, and some GGUF model servers may end early.
The default Auto mode preserves older text profile lengths. The settings are stored locally and can change only when the
brain is stopped. The process still binds **only 127.0.0.1:43821**, with
`--jinja` for reviewed tool calling. The chat archive can be arbitrarily
long while each model request still has finite context. Large context and
full GPU offload can exceed the RTX4070 12 GiB VRAM: start with 8,192 and
reduce GPU layers or context if model loading fails. The Start/Stop control
now permits stopping a brain during cold loading; after a three-minute
startup timeout Ember terminates its owned inference process rather than
leaving a stalled process consuming VRAM. This does not establish inference
compatibility, performance or reasoning quality of an untested model.

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

**2026-11-22 native tap/hold dictation:** The Chat microphone control now starts local RU/EN capture immediately on button press. A tap shorter than 450 ms leaves recording active so the user can choose **To draft**, **Transcribe & send**, or **Cancel**. Holding the button for at least 450 ms, then releasing, stops capture and transcribes into the editable draft only; this action does **not** submit the text to the LLM. Dragging off the pressed control cancels capture and discards its audio. The control remains visible and enabled while held so the matching release is not lost. The recognition language picker and local file Browse controls cannot change the currently transcribing model/language mid-turn. The recording remains user-controlled rather than ending at a fixed 30-second limit; real Windows microphone/ASR acceptance is still required.\n
**Dictation draft insertion safety:** Transcribed RU/EN text is inserted exactly at the current editor caret, with whitespace determined on each side of the caret. A long transcription is never silently truncated merely because it exceeds the LLM's 8,192-character single-message limit; the UI instead warns and disables Send until the user edits or splits the draft. Ctrl+Enter still enters the backend's rejection path and cannot bypass the limit. This applies to both tap-to-record and hold/release-to-draft.\n
**2026-11-22 Whisper terminology parity:** User-entered per-chat RU/EN recognition vocabulary is now applied to both locally managed ASR adapters. Native Qwen3-ASR continues to receive its bounded multipart `prompt`; `whisper-cli` now receives a separate `--prompt` argument with the same whitespace-normalized, 400-character maximum. No shell, no remote service and no automatic model downloads are introduced. Official upstream CLI source: https://github.com/ggml-org/whisper.cpp/blob/master/examples/cli/cli.cpp . This may improve proper nouns but is not a measured WER improvement; run real RU/EN corpus tests before treating it as quality acceptance.\n
**2026-11-22 finite-context guard for mixed RU/EN and long technical chats:** The live app now passes the selected 4K/8K/16K/32K context into chat request construction. Instead of blindly reusing up to 12,000 characters of prior dialogue, it conservatively budgets history using UTF-8 size and template overhead, drops old turns first, and reduces requested output tokens when required. A too-large *current user prompt* causes a visible rejection with the entire editable draft intact — it is never silently truncated to fit. A new Qt test covers Russian Unicode, 4K vs 16K, long prior history and backward-compatible helper defaults. **This is not a tokenizer-exact guarantee**: tokenizer templates and GGUF vocabularies vary, and only real llama.cpp inference can confirm actual context usage. No changes to external network access, release policy, or schedules.\n
**2026-11-22 real local model token inspection:** The explicitly invoked Windows `python tools/offline_acceptance.py --root ... --run` now checks fixed EN/RU diagnostic prompts against the *actual local GGUF's* `/apply-template` and `/tokenize` APIs after its ordinary streamed instruction check. It records model-specific prompt token counts, active context and reserved reply tokens without retaining generated text or real conversation history. All requests go to PID-verified `127.0.0.1:43821` with redirects/proxies disabled; malformed token IDs, missing JSON, unexpectedly large responses, changed socket owners and a sample context overrun fail explicitly. The default invocation remains read-only with no model launch, and the CI self-test uses only local response-shape fixtures. This helps measure the new heuristic context guard but cannot guarantee all arbitrary user prompts fit. Upstream documentation: https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md .\n