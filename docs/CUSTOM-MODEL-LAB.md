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

## Dataset schema (original user-authored example; do not copy private chats)

Each line is one JSON object:

```json
{"schema":"ember-mind-sft-v1","sample_id":"c01","group_id":"conversation-001","task":"conversation","language":"en","rights":"user-authored","messages":[{"role":"user","content":"I've been working all day. What should I do now?"},{"role":"assistant","content":"I'd probably take a break before deciding. Was it productive work, or mostly frustrating?"}]}
```

Allowed `task`: `conversation`, `coding`, `troubleshooting`, `story`, `roleplay`, `tool-use`; `language`: `en`, `ru`, `mixed`; `rights`: `user-authored`, `licensed`, `permissive`. For `licensed` or `permissive`, include a nonempty `license` identifier. Multiple examples in one `group_id` always remain on the same side of the train/eval split. At least two independent groups per included task are required. `report.json` contains counts and checksums, not transcripts. Source-rights fields are the curator's declarations: they cannot prove legal ownership.

A dataset with unverified low-quality synthetic answers will amplify mistakes. Review output samples before training and perform independent blind user evaluation. Avoid using model outputs as authoritative ground truth for coding or troubleshooting.

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
