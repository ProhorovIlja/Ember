# EmberMind and Ember Voice — custom model engineering plan

**Status (2026-11-23): architecture and local dataset preparation are implemented. No custom-trained model weights, completed fine-tune, merged checkpoint or human-rated audio are claimed.** This document replaces the mistaken interpretation that Ember should merely offer an ever-changing list of foreign models.

## User-facing requirement

Ember should feel like **one privately installed assistant with one stable identity**, able to talk naturally in English (female synthesized voice), handle EN/RU text and dictation, develop/debug code, research, write stories and roleplay, use permissioned local/cloud tools, and preserve scoped long conversations. Mode selection must not require the user to pick model file names. Heavy models may run sequentially or on different hardware resources *internally*. The interface stays native Qt and English, with no mandatory cloud login, background tasks, or first-run downloads.

## Target model artifacts (separate by modality, unified as one product)

- **EmberMind**: a *single* locally served, fine-tuned text/vision-capable language model with consistent persona and instructions. A tested 4B checkpoint is the first feasible training target on the given 12 GB GPU; a quantized Qwen3.5-9B post-trained checkpoint is the quality baseline for inference and a possible later teacher/student/adapter target. Running 9B QLoRA on 12 GB is **not guaranteed**: the precise implementation, sequence length, optimizer, activation offloading and GPU occupancy govern peak memory. Directly training a 9B bf16 LoRA may exceed 12 GB.
- **Ember Voice**: a separately fine-tuned English female acoustic model, with stable speaker identity and expressive prosody. Qwen3-TTS 0.6B/1.7B **Base** has official single-speaker fine-tuning instructions. Qwen3-TTS CustomVoice/VoiceDesign is a distinct inference/conditioning model: do not confuse CustomVoice style control with an already trained, user-owned voice. Separate ASR (whisper.cpp/Qwen3-ASR) is necessary for accurate speech input.
- **Ember Tools and Memory**: safe, permission-scoped filesystem, search, coding, GitHub/Drive and session retrieval logic. These are *capabilities of an application*, not additional neurons created by merging LLM weights.

One assistant is a **product identity**, not a claim that all audio, vision, retrieval, tool calls and language logic are mathematically one tensor.

## Experiment sequence

1. **Freeze baseline and tests.** Use Qwen3.5-4B post-trained as practical small-model candidate and Qwen3.5-9B post-trained as quality reference. Retain upstream model/revision/tokenizer/source license SHA-256 and the exact CUDA/llama.cpp build used for evaluation. First verify correct thinking-on/off, prompt template, multilingual/code function-call outputs.
2. **Collect high-quality legal examples.** Human-authored or explicitly licensed conversations, coding reviews, debugging procedures, stories, roleplay continuity and properly structured tool protocols. Good/bad preference pairs require separate review; avoid scraped copyrighted conversations, private history without consent, speculative model-generated labels and unverified 'uncensored' training sets. Sample data should teach concise natural interactions rather than robotic or excessive roleplay.
3. **Curate train/evaluation separation locally.** `python tools/ember_mind_studio.py prepare --input C:\\private\\ember-examples.jsonl --output-dir C:\\private\\ember-sft-v1` creates only `train.jsonl`, `eval.jsonl`, `report.json`. It refuses to overwrite folders, groups related conversations into the same partition, preserves original model-facing message roles, rejects duplicate text and declares rights metadata. No network, model downloads, training or scheduled execution. Holdout examples are **not** a substitute for outside benchmarks. Both train/eval contain plaintext examples, remain on the PC, and must not be committed or synchronized to Google Drive.
4. **Fine-tune a compatible language checkpoint.** Evaluate Axolotl or Hugging Face TRL/PEFT QLoRA with an actual *supported* Qwen3.5 model implementation, conservative sequence lengths, small batch and gradient checkpointing. Verify on the exact RTX 4070 before committing to a costly run. Early work may keep the original post-trained model as the base to reduce catastrophic forgetting; compare against training from the official Base weights when appropriate. Preserve untouched original.
5. **Optionally train specialist adapters against the same base/revision/tokenizer.** Natural conversation; coding/tools; fiction/roleplay. Fuse adapters with PEFT or compatible mergekit TIES/DARE only after verifying source checkpoints and matching tensor dimensions, and run regression A/B on every target. A 4B adapter cannot simply be dropped into a 9B architecture. An adapter from a different tokenizer/architecture is not presumed mergeable.
6. **Prefer a single useful checkpoint if tests support it.** LoRA `merge_and_unload()` can fold a compatible adapter into the base weights, then export/quantize to GGUF and accept it into the existing llama.cpp runtime. Verify quality after GGUF conversion. If a fused model loses code or dialogue ability, revert; keep separate internal specialists **only if justified by evidence and memory budget**.
7. **Train/condition Ember Voice independently.** Use legally sourced, consented English female speech with exact transcriptions and varied prosody (neutral, curious, relieved, intimate, urgent, technical). Start from Qwen3-TTS official **Base** fine-tuning instructions and benchmark expressive syllables and speaker identity against Qwen3-TTS CustomVoice and Kokoro. Do not imply the open Sesame CSM model is the fine-tuned production Maya voice.
8. **Tune concurrency and packaging.** A single RTX 4070 12 GB does not guarantee 9B LLM + 1.7B TTS simultaneous GPU residency. Prefer CPU/ONNX voice fallback or managed handoff if audio quality/performance is acceptable; measure actual p95 first audible sound, token/s, WER/CER, memory and CPU/GPU power. Package *only* licensed model files with source, hashes and offline test evidence.
9. **Promotion is gated.** Every customization must beat or match the original model on English/Russian natural conversation, code and actual compiler/tests, troubleshooting, long-story character consistency, trustworthy citations, tool-call correctness, refusal behavior appropriate to the user's desired writing style, prompt-injection resistance for tools, retention/privacy, and speed. The native application and human voice must pass the existing 17 stable-release gates.

## 23 November 2026 — strengthened EmberMind training/evaluation integrity

The independent model-evaluation set can produce **falsely high scores**
when the same user question is repeated in another `group_id` with a
different assistant answer. Merely hashing whole conversation objects
does not prevent that leakage. EmberMind Studio now fingerprints only
the *user turns* of each fully validated sample, using Unicode NFKC,
case-folding and whitespace collapse **for comparison only**. If those
user-turn sequences appear in two distinct source conversation groups,
the preparer rejects the dataset before creating train/eval files.
The actual written training messages are **never normalized or edited**.

The generated Axolotl QLoRA recipe now also targets Qwen3.5's
`linear_attn.in_proj_qkv`, `linear_attn.in_proj_z` and
`linear_attn.out_proj` Gated DeltaNet modules, in addition to standard
attention/MLP LoRA targets. This follows the official Axolotl
[Qwen3.5 guidance](https://docs.axolotl.ai/docs/models/qwen3.5.html)
rather than assuming that the hybrid model consists only of ordinary
Transformer attention. **Whether all modules are present, train correctly,
and fit alongside optimizer/activation state in 12 GB VRAM is not yet
verified on the real base checkpoint/hardware**; an official architecture
reference and synthetic YAML assertion do not constitute a completed
training run.

The separate `ember_mind_qlora.py` preflight now independently validates
the finished OpenAI-style `messages` array (object fields, required
string content, user/assistant alternation with optional initial system
message, assistant ending, length bounds and forbidden NUL).
It rejects duplicate JSON keys, duplicate full training dialogues,
and normalized **train-versus-eval** user-prompt overlaps even when an
operator has modified both JSONL files *and updated their report hashes*.
Malformed trailing messages are rejected as ordinary validation errors
instead of triggering a Python attribute exception.

Both tools have explicit **synthetic-only regression tests**: duplicate
prompts under different IDs/groups, changed assistant completion,
different capitalization/whitespace, non-object messages, wrong role
order, extra fields and duplicate JSON keys. Neither tool requires GPU
or downloads models. This is a stricter hygiene gate, **not a semantic
duplicate detector**: paraphrases of the same prompt, shared benchmark
sources, contaminated teacher answers and mistakes in source-rights
declarations still require manual audit. No custom model training or
quality comparison has yet taken place. An explicit user-initiated
one-shot training command and genuinely obtained licensed weights/data
are still prerequisites.

Official compatibility reference:
[Axolotl Qwen3.5](https://docs.axolotl.ai/docs/models/qwen3.5.html)
documents hybrid DeltaNet/Transformer support, not proven QLoRA
peak memory on Ember's RTX 4070 12 GB.
[Qwen3.5-4B-Base](https://huggingface.co/Qwen/Qwen3.5-4B-Base)
is the publicly available trainable checkpoint (Apache 2.0);
cloud-only Qwen4-Max cannot substitute for local open weights.
Dolphin post-trained checkpoints are behavior/quality comparators,
not tensor-compatible components one can blindly paste into Qwen3.5.

## Dataset schema (original user-authored example; do not copy private chats)

Each line is one JSON object:

```json
{"schema":"ember-mind-sft-v1","sample_id":"c01","group_id":"conversation-001","task":"conversation","language":"en","rights":"user-authored","messages":[{"role":"user","content":"I've been working all day. What should I do now?"},{"role":"assistant","content":"I'd probably take a break before deciding. Was it productive work, or mostly frustrating?"}]}
```

Allowed `task`: `conversation`, `coding`, `troubleshooting`, `story`, `roleplay`, `tool-use`; `language`: `en`, `ru`, `mixed`; `rights`: `user-authored`, `licensed`, `permissive`. For `licensed` or `permissive`, include a nonempty `license` identifier. Multiple examples in one `group_id` always remain on the same side of the train/eval split. At least two independent groups per included task are required. `report.json` contains counts and checksums, not transcripts. Source-rights fields are the curator's declarations: they cannot prove legal ownership.

A dataset with unverified low-quality synthetic answers will amplify mistakes. Review output samples before training and perform independent blind user evaluation. Avoid using model outputs as authoritative ground truth for coding or troubleshooting.

## Actual adapter provenance and merge-readiness checks

The optional **local-only**, standard-library preflight is
[`tools/adapter_merge_preflight.py`](../tools/adapter_merge_preflight.py).
It requires real PEFT LoRA `adapter_config.json` and
`adapter_model.safetensors`, the official base checkpoint's local `config.json`
and `tokenizer.json`, and a private `report.json` generated by
`ember_mind_studio.py`. It does **not** run a model or merge weights.

After obtaining legally usable source files and training each specialist
adapter, pin provenance separately for each adapter with an **exact 40-character
base model commit SHA** (not a mutable branch/tag):

```powershell
python tools/adapter_merge_preflight.py lock --adapter "D:\Ember\adapters\dialogue" --base-id "Qwen/Qwen3.5-4B" --base-revision "<actual-40-hex-base-commit>" --base-config "D:\Ember\base\config.json" --tokenizer "D:\Ember\base\tokenizer.json" --dataset-report "D:\Ember\private\dialogue-data\report.json" --task conversation --license "Apache-2.0"
python tools/adapter_merge_preflight.py lock --adapter "D:\Ember\adapters\coding" --base-id "Qwen/Qwen3.5-4B" --base-revision "<same-actual-40-hex-base-commit>" --base-config "D:\Ember\base\config.json" --tokenizer "D:\Ember\base\tokenizer.json" --dataset-report "D:\Ember\private\coding-data\report.json" --task coding --license "Apache-2.0"
python tools/adapter_merge_preflight.py check --adapter "D:\Ember\adapters\dialogue" --adapter "D:\Ember\adapters\coding"
```

Do not copy the angle-bracket placeholder literally. The tool refuses
overwriting a provenance lock, rejects adapter weight/config changes and mismatched
base revisions/configurations/tokenizers/LoRA signatures. It deliberately rejects
advanced LoRA transformations requiring separate tensor-level analysis.
**Important:** The stated base revision, data-rights and model identities are
operator assertions; the tool cannot independently authenticate a model
download, actually parse tensor shapes, or establish successful training.
After a PASS, still perform a real merge with PEFT/MergeKit and evaluate its
outputs against both original adapters and the untouched checkpoint. Store
training examples privately; only aggregate test results belong in Git.

## Concrete local QLoRA run — opt-in, offline, experimental

The code in [\`tools/ember_mind_qlora.py\`](../tools/ember_mind_qlora.py)
goes beyond curation: it generates a **locally executable Axolotl training
configuration** for the original full Hugging Face **Qwen3.5-4B** model
(post-trained or Base checkpoint, subject to user-chosen source), pinned
dataset and 12 GB-friendly conservative hyperparameters. It can also invoke
one locally installed Axolotl command, but only on explicit user request
with a separate CLI switch.

**Do not confuse Qwen3.5 GGUF inference files with trainable Hugging Face
safetensors.** The latter must have all original model shards, config, tokenizer
and an independently recorded exact upstream model revision. The preflight verifies metadata and SHA-256 hashes **every actual
safetensors shard**. The explicit training command recomputes them to reject
a same-size weight change between preparation and launch. These checks do not
authenticate the model's origin, prove Axolotl compatibility or establish
a 12 GB VRAM fit. Hashing the full checkpoint requires disk I/O.
It does not download anything. Axolotl/CUDA/FlashAttention installation is a
separate user-controlled environment preparation; the native Windows Ember
application does not require those training libraries. Linux or WSL2 with
the right CUDA toolkit is a reasonable training environment.

1. Prepare human-reviewed **private** data using \`ember_mind_studio.py\`
   outside the Git repository. A normal run produces \`train.jsonl\`,
   \`eval.jsonl\` and \`report.json\` with explicit hashes and independent
   grouped holdout.
2. With model files and Axolotl already installed locally, generate a recipe
   without training:

   \`\`\`sh
   python tools/ember_mind_qlora.py prepare \
     --base-dir /mnt/d/Ember/private/qwen35-4b-hf \
     --dataset-dir /mnt/d/Ember/private/sft-v1 \
     --output-dir /mnt/d/Ember/private/qlora-v1 \
     --base-revision ACTUAL_40_HEX_COMMIT_SHA \
     --sequence-len 1024 --epochs 1
   \`\`\`

3. Independently inspect \`qlora-v1/axolotl.yaml\`, confirm the exact
   Axolotl version/available GPU resources, and **manually** begin one run
   only when desired:

   \`\`\`sh
   python tools/ember_mind_qlora.py train \
     --run-dir /mnt/d/Ember/private/qlora-v1 \
     --i-approve-one-shot-training
   \`\`\`

The train entry point sets Hugging Face offline environment variables and
disables telemetry, and rechecks the source data and metadata fingerprints.
**There is no scheduled training, automatic retries or cloud upload.**
The initial YAML uses Qwen3.5's \`qwen3_5\` chat template, QLoRA 4-bit,
LoRA rank 16, 1024-token sequences, no sample packing and gradient
checkpointing. This is a candidate, NOT a benchmarked configuration;
it may run out of 12 GB VRAM or need training-stack fixes. It excludes
evaluation data from training; after training, compare against held-out,
independent and real development/voice cases. A genuine trained adapter
can later be pinned with \`adapter_merge_preflight.py\` and evaluated before
any merge. Even a completed fine-tune cannot guarantee a better model
without independent quality evidence.

## Concrete single-speaker Ember Voice training corpus

[\`tools/ember_voice_corpus.py\`](../tools/ember_voice_corpus.py)
prepares the **official Qwen3-TTS Base fine-tuning input format**:
\`{"audio": "...wav", "text": "...", "ref_audio": "...wav"}\`
per JSONL line. It requires an explicit positive consent declaration for every listed WAV,
verifies mono 16-bit 24 kHz PCM, bounds duration, rejects badly silent or
clipped material, unsafe paths and duplicate WAV **content**, and splits
recording sessions for evaluation. The reference voice clip must itself be
a consent-declared manifest entry from a **training-side** recording session;
using held-out evaluation audio as `ref_audio` is prohibited. These
declarations cannot independently authenticate recording rights, identity
or transcript correctness.
A private input manifest row should look like:

\`\`\`json
{"schema":"ember-voice-source-v1","audio":"line001.wav","text":"I was wondering if you'd like to talk for a while.","session":"session-001","consent":true}
\`\`\`

Create multiple such user-authorized utterances across at least two
independent recording sessions; the program insists on at least four
clips for the *software* preflight, but a genuinely natural stable voice
requires **far more** clean, phonetically varied, consented speech and
blind listening tests. A reference `ref.wav` must come from the same authorized speaker,
have its own positive consent row in the manifest, and belong to a training
session rather than an evaluation session. For example:

\`\`\`sh
python tools/ember_voice_corpus.py prepare \
  --audio-root /mnt/d/Ember/private/voice-clips \
  --manifest /mnt/d/Ember/private/voice-manifest.jsonl \
  --reference ref.wav \
  --output-dir /mnt/d/Ember/private/voice-v1
\`\`\`

This creates private \`train_raw.jsonl\`, \`eval_raw.jsonl\` and an aggregate
\`report.json\` without moving audio or recording/transcribing anything.
The output is ready as **input** for Qwen's published
\`finetuning/prepare_data.py\` audio-code step, followed by its
\`sft_12hz.py\` script against the correct local **0.6B/1.7B Base**
checkpoint. Those upstream scripts have not been invoked or validated
on the target RTX 4070, and the manifest tool does not train an acoustic
model. Official 12 Hz Base is single-speaker training only; this should not
be marketed as a Maya clone or guaranteed expressive voice.

No personal recordings, transcripts, training shards or checkpoints are
part of GitHub commits, Google Drive engineering notes or test fixtures.
\`.gitignore\` additionally excludes common dataset and model extensions.
The built-in self-tests use tiny synthetic waveforms and sparse placeholder
model files; they never use a person's voice.

## What this does **not** provide yet

- No actual weights are downloaded, fine-tuned, merged or evaluated in GitHub CI.
- No model architecture has been established as universally best or equivalent to GPT-6/large cloud agents.
- No single TTS/LLM weight merge or automatic transfer of 'best qualities' across incompatible architectures.
- No verified 12 GB training throughput, audio naturalness or seamless no-VPN offline Windows package.
- No automatic package/model updates. Any future online update is strictly initiated by the user and must support pinning/hash checks and rollback.

## Upstream research and compatibility

- Qwen3.5-4B-Base (Apache-2.0, designed for PEFT): https://huggingface.co/Qwen/Qwen3.5-4B-Base
- Qwen3.5-9B-Base and post-trained models: https://huggingface.co/Qwen/Qwen3.5-9B-Base and https://huggingface.co/Qwen/Qwen3.5-9B
- Qwen3.5 special DeltaNet/Transformer fine-tuning caveats: https://docs.axolotl.ai/docs/models/qwen3.5.html
- TRL/PEFT LoRA/QLoRA: https://huggingface.co/docs/trl/peft_integration
- MergeKit TIES/DARE and compatibility: https://github.com/arcee-ai/mergekit/blob/main/docs/merge_methods.md
- PEFT model/adapters merging: https://huggingface.co/docs/peft/developer_guides/model_merging
- Qwen3-TTS official 1.7B/0.6B **Base** single-speaker fine-tuning: https://github.com/QwenLM/Qwen3-TTS/blob/main/finetuning/README.md
- Existing Ember acceptance: https://github.com/ProhorovIlja/Ember/blob/voice-foundation/docs/RELEASE-GATES.md
