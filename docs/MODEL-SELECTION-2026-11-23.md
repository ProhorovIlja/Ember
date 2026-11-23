# Ember model and stack selection — 23 November 2026

This is a **candidate and acceptance matrix, not a claim of measured superiority**. Ember is a native Windows app for an RTX 4070 12 GB / Ryzen 5 5600 / 32 GB system, prioritizing English female speech, English/Russian text and dictation, human-sounding dialogue, real coding/troubleshooting tools, and an offline/no-first-run-download package. UI is English. No scheduled or background updates.

## Decision: modular component composition, not incompatible weight merging

Keep the existing C++20/Qt 6 native application and narrow provider interfaces. C++/Qt currently meets the low-overhead GUI/PCM and Windows integration target; switching to Rust or another language without profiling would add risk, not demonstrated performance. Python may remain a **development/benchmark/optional model runtime** for models that need it; avoid Python or a web server as a mandatory UI runtime. Rust can be evaluated for isolated helpers if there is a measurable benefit. Evaluate technologies on real memory, maintenance, security, licensing and packaging evidence, not language popularity.

Do NOT merge unrelated text, ASR and TTS weights. They have different architectures, tokenizers and losses. Compose them through typed, local interfaces, with independently installable model packages, explicit selection and rollback. A LoRA fine-tune or a text-model merge is an optional compatible-model experiment, not a magic way to add reasoning or a Maya voice.

## Local text shortlist — re-evaluate after actual benchmarks

| Candidate | Role | Strong reason to test | Blockers / caveats |
|---|---|---|---|
| Qwen3.5-9B post-trained, Q4_K_M GGUF | Primary balanced conversation, coding, RU/EN, optional image understanding with proper mmproj | Apache-2.0, thinking/nonthinking; quantized community build around 5.84 GB *weights only* | Prove GGUF/llama.cpp version compatibility and response quality; KV/cache and TTS consume extra VRAM |
| Qwen3.5-4B post-trained | Fast conversational/voice brain, fallback while heavier TTS occupies GPU | Lower memory footprint, same family | May sacrifice troubleshooting and code quality |
| Qwen3-8B | Controlled comparison / compatibility reference | Already documented in Ember | No assumption that older generation beats 3.5 |
| Qwen4-Max | **Cloud research reference, not an offline candidate** | Alibaba released API model on 16 Nov 2026 | No released Max weights; cannot ship/install locally |
| Dolphin instruction fine-tunes | Optional personality/alignment A/B research, only with exact base model/weights/license | Different response style and less refusal-oriented training may fit some roleplay | Dolphin is a *family of tuned LLMs*, not an independent memory/reasoning processor; it cannot be attached to Qwen3.5 as a universal "brain" plugin |

Official Qwen3.5-9B inference examples differentiate non-thinking and thinking sampling (temperature, top-p, top-k and presence penalty). **Do not assume one global temperature is optimal for both**, or that optimizing text sampling changes synthesized vocal timbre. Tune defaults per actual tested family/mode; preserve user overrides and provide an Auto option. Current Ember settings are a foundation; there is no human-like naturalness benchmark result yet.

## English female voice shortlist (must be heard blind on target PC)

1. **Qwen3-TTS CustomVoice 1.7B via native speech.cpp**: current expressive primary candidate; instructions for speaking style, potentially richer prosody; must measure how it competes with resident Qwen3.5-9B for 12 GB VRAM, latency, speaker stability.
2. **Kokoro-82M English v0.19 via sherpa-onnx**: very light CPU fallback, stable output and low combined GPU memory; less flexible prosody.
3. **Qwen3-TTS 0.6B**: quality/performance reference against 1.7B and Kokoro. The supported native adapter does not expose 1.7B's detailed delivery control on the smaller model.
4. **Chatterbox Turbo / Nano**: promising voice-expression / CPU candidate with paralinguistic tags; original publisher marks project retired as of Aug 2026, so maintenance and reproducible Windows packaging are concerns. Do not make it the sole production engine without adopting and testing the code.
5. **Sesame CSM-1B**: worthwhile context-conditioned research comparator, **not** the publicly accessible Maya model; Sesame says its interactive demo runs a fine-tuned version of CSM. Downloading base CSM does not reconstruct Maya.

Additional research-only expressive voices can be compared after the existing Kokoro/Qwen3/native path works. A text LLM and a TTS should not both remain on the GPU just because each one fits separately.

## ASR, tools, storage, updates

- RU/EN dictation: compare multilingual whisper.cpp and speech.cpp Qwen3-ASR 0.6B/1.7B on the exact same owned audio corpus, with WER/CER, dropped words, punctuation, mixed language and 5+ minute capture. Use CPU ASR as a fallback when LLM/TTS occupy VRAM.
- Reasoning: use the LLM's supported thinking mode and structured tools. Persistent memory is a **separate retrieval system** (SQLite + opt-in embeddings/index, citations and scoped permissions); "Dolphin" is not equivalent to long-term memory or an agent.
- Native tools: preserve explicit per-operation user approval and audit before local filesystem writes, project tests, GitHub mutations and Drive access. Do not silently use private files or leak them to an LLM vendor.
- Offline installer: pinned versions + SHA-256 + model licenses/NOTICE + runtime DLLs; no downloads on first launch. Later **user-initiated** online updates can check signed/hash-verified manifests and support rollback. No automatic checks, tasks, monitoring or scheduled activity.
- No assumption that complete ChatGPT-class general intelligence can be obtained from a single quantized 4–9B model, regardless of GUI polish or prompt tuning.

## Human-quality text comparison — tool added

Use tools/blind_text_ab.py, which runs against the user-started 127.0.0.1:43821 endpoint with *only original built-in test prompts* and fixed sampling; it never sends anything to a cloud endpoint. Run it once for each model after manually stopping and selecting the model. Captures store generated synthetic test answers in local JSON **in plaintext** and are not uploaded. A subsequent offline score step randomizes speaker/model order and asks which is more natural/useful. Only scored categories and model labels, **no answers**, go into the score report. The Windows CI self-test validates protocol/safety, but does not run models or measure voice/text quality.

Example on the PC (same hardware-label and parameters both times):

    python tools/blind_text_ab.py capture --label qwen3.5-9b-Q4KM --hardware-label win10-ryzen5600-rtx4070 --output qwen35.json
    # Stop old server, select and explicitly start another local GGUF.
    python tools/blind_text_ab.py capture --label qwen3.5-4b-Q4KM --hardware-label win10-ryzen5600-rtx4070 --output qwen35small.json
    python tools/blind_text_ab.py score --baseline qwen35.json --candidate qwen35small.json --output text-scores.json

These matched fixed-sampling tests rank user preference, **not universal truth**. After a fair baseline, repeat with each model's manufacturer-supported *tuned* sampling and a separately labeled experiment; do not conceal different configs in a paired comparison. Tool-calling reliability, factual correctness and multi-hour stability require separate tests. Never substitute a synthetic success for real target-device acceptance.

## Release priority and remaining proof

Current status: code compiles in GitHub Windows CI, while 17 release gates and real hardware tests remain unresolved. Priority: (1) licensed offline weights and verified Windows engines; (2) voice A/B listening + RU/EN dictation + LLM benchmark on RTX 4070; (3) prevent VRAM oversubscription and test model switching; (4) agent permissioned operations, long-memory correctness, backups and privacy; (5) clean offline Windows install, release QA, notes and checksum. This is a plan, not a schedule or completion guarantee.

### Upstream references (source pages; accessed 2026-11-23)

- Qwen3.5-9B model card and sampling: https://huggingface.co/Qwen/Qwen3.5-9B
- Qwen3.5-4B: https://huggingface.co/Qwen/Qwen3.5-4B
- Example quantized Qwen3.5-9B: https://huggingface.co/unsloth/Qwen3.5-9B-GGUF
- Qwen4-Max release reporting and weight status: https://www.reuters.com/business/retail-consumer/alibaba-unveils-new-flagship-ai-model-with-safety-upgrades-2026-11-16/
- Native Qwen speech engine: https://github.com/nyosegawa/speech.cpp
- Qwen3-TTS upstream: https://github.com/QwenLM/Qwen3-TTS
- Kokoro: https://huggingface.co/hexgrad/Kokoro-82M
- Chatterbox maintainer notice: https://github.com/resemble-ai/chatterbox
- Sesame CSM demo vs base distinction: https://github.com/SesameAILabs/csm
- Dolphin example family (not a modular brain processor): https://huggingface.co/TheBloke/dolphin-2.6-mistral-7B-GGUF
