# Ember — manual Qwen3.5 / llama.cpp runtime acceptance

This is a **foreground, user-invoked test**, not a scheduled job. It validates
the exact spoken/text/Deep **language-model protocol** that Ember intends to
use with a selected local Qwen3.5 GGUF. It does NOT verify audible speech,
model naturalness, ASR, correctness of arbitrary code, trust of an installed
GGUF's provenance, or successful custom model training.

## Before testing

Start Ember's **Local Brain** with a user-supplied, known-compatible Qwen3.5
GGUF and its supported managed Windows `llama-server`. Wait for the native
UI to show that the model is ready on `127.0.0.1:43821`. Keep this
user-started process running. No downloads or model servers are started by
the following test.

The probe is Python standard-library-only. Run from the source checkout:

```powershell
py -3 .\tools\brain_mode_acceptance.py --self-test
$listener = Get-NetTCPConnection -LocalAddress 127.0.0.1 -LocalPort 43821 -State Listen | Select-Object -First 1
if (-not $listener) { throw "Start the local Ember brain first" }
py -3 .\tools\brain_mode_acceptance.py --pid $listener.OwningProcess --output "$env:USERPROFILE\Documents\ember-brain-acceptance.json"
```

The output path must **not already exist**; choose a new filename for a
separate explicit run. Or omit `--output` to print metrics without creating
a report. No raw model replies, thoughts, personal conversation snippets,
file paths, tokens or passwords are included in reports.

The test itself checks the Windows TCP listening **PID** before and after
each call, binds only to exact IPv4 `127.0.0.1:43821`, refuses system
proxies/redirects, and never accepts a remote user-configurable endpoint.
PID checks are not full cryptographic process authentication and cannot
exclude extremely short-lived port-rebind races.

## Four real requests

1. English normal/voice-style, `enable_thinking=false` and nonthinking
   sampler; exact synthetic reply `EMBER_READY`.
2. Russian normal text, `enable_thinking=false`; exact synthetic reply
   `ГОТОВО`.
3. English general Deep, `enable_thinking=true` and **general thinking**
   sampler.
4. English code analysis Deep, `enable_thinking=true` and **precise coding**
   sampler.

The probe rejects malformed or incomplete SSE, `finish_reason=length`,
missing visible text, raw `<think>` markers in output, nonthinking
`reasoning_content` leakage, inappropriate protocol payloads, truncated
exact-match answers and unwanted extra content. It measures p50
first-*visible-text* latency, not first physical audio and not first
HTTP byte. Deep requests may legitimately contain separate private
`reasoning_content`; only its character count is reported, never its text.

Qwen3.5 presets follow the [official Qwen3.5-9B model card](https://huggingface.co/Qwen/Qwen3.5-9B).
Some `llama.cpp` revisions ignore `chat_template_kwargs.enable_thinking=false`,
or misclassify visible output as reasoning. A failed probe is actionable
compatibility evidence but **not** an automatic fix and is never silently
converted into a pass. Pin your manually installed GGUF, runtime version
and GPU driver when interpreting results.

Do **not** use `tools/text_benchmark.py` alone to declare Qwen3.5
voice-readiness: its generic SSE benchmark does not set the Qwen3.5
nonthinking chat-template flag and can measure hidden-reasoning latency
instead of ordinary Ember dialogue.

## What a PASS establishes (and what it does not)

- The installed model/server combination produced four valid synthetic
  streamed responses using the requested sampling and Qwen template flags.
- Normal-mode replies had no exposed reasoning and followed two short
  exact-output instructions in English and Russian.
- The local HTTP SSE contract completed, with bounded output.

It does **not** prove optimal conversation style, long-term memory, tool
correctness, full-duplex voice, Gemini/ChatGPT parity, fine-tuning success or
12 GB GPU co-residency with TTS. To test code tools separately, manually run
`python tools/text_benchmark.py --tool-verify`: this fixture tests protocol
only, executes no real file edits and must not be counted as a coding
quality score. To compare naturalness, manually use
`tools/blind_dialogue_ab.py` on distinct loaded models and human-review
the complete three-turn dialogues; synthetic self-tests do not establish
model quality.

## Integrity

Local verified synthetic run on 23 Nov 2026:

```text
brain_mode_acceptance: 4 payload and 9 stream-fixture checks passed
```

This run used **no** Qwen model, CUDA, RTX 4070 or physical speech device.
The source file's tested Git blob is
`177a02e80192cb798b87e3075b0ff804b5412d96`. No Windows native
build, real model benchmark, scheduled check, automatic research, CI
workflow or public Release was triggered by this manual probe work.
