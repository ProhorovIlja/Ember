#!/usr/bin/env python3
"""Generate and optionally launch a local-only Axolotl Qwen3.5-4B QLoRA run.

Consumes EmberMind Studio private train/eval JSONL; never downloads weights,
uploads prompts, runs scheduled jobs, or enables telemetry. "prepare" only
writes a configuration. "train" requires a second explicit command.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path

BASE_MODEL_TYPE = "qwen3_5"
SCHEMA = "ember-mind-qlora-run-v1"
REVISION = re.compile(r"[0-9a-f]{40}\Z")
SPLIT_SCHEMA = "ember-mind-curation-report-v1"
DATA_LIMIT = 64 * 1024 * 1024

def fail(message):
    raise ValueError(message)

def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def read_json(path, max_bytes=262144):
    p = Path(path)
    if not p.is_file() or p.is_symlink() or not 0 < p.stat().st_size <= max_bytes:
        fail("Invalid local metadata file: " + str(p))
    try:
        result = json.loads(p.read_text(encoding="utf-8"))
    except (UnicodeError, json.JSONDecodeError):
        fail("Invalid JSON metadata: " + p.name)
    if not isinstance(result, dict):
        fail("Expected JSON object: " + p.name)
    return result

def require_file(path, limit):
    p = Path(path)
    if not p.is_file() or p.is_symlink() or not 0 < p.stat().st_size <= limit:
        fail("Invalid or missing private file: " + str(p))
    return p

def require_dir(path):
    p = Path(path)
    if not p.is_dir() or p.is_symlink():
        fail("Not a real local directory: " + str(p))
    return p.resolve()

def inspect_dataset(directory):
    root = require_dir(directory)
    report = read_json(root / "report.json")
    if report.get("schema") != SPLIT_SCHEMA:
        fail("Dataset must come from EmberMind Studio")
    summaries = {}
    for name in ("train", "eval"):
        p = require_file(root / (name + ".jsonl"), DATA_LIMIT)
        observed = digest(p)
        if observed != report.get(name + "_sha256"):
            fail("Dataset report hash does not match " + name)
        count = 0
        with p.open(encoding="utf-8") as handle:
            for line in handle:
                if len(line) > 512000 or not line.strip():
                    fail("Blank or too large training line")
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    fail("Invalid JSONL in " + name)
                if not isinstance(obj, dict) or set(obj) != {"messages"}:
                    fail("Axolotl inputs must contain messages only")
                messages = obj["messages"]
                if not isinstance(messages, list) or len(messages) < 2:
                    fail("Invalid message turn list")
                if messages[-1].get("role") != "assistant":
                    fail("Examples must end with an assistant message")
                count += 1
        if count != report.get(name + "_examples") or count < 1:
            fail("Dataset example count mismatch")
        summaries[name] = {"path": str(p.resolve()), "sha256": observed,
                           "examples": count}
    if summaries["train"]["sha256"] == summaries["eval"]["sha256"]:
        fail("Train and evaluation datasets must be different")
    return summaries

def inspect_base(directory):
    root = require_dir(directory)
    config = read_json(root / "config.json")
    if (config.get("model_type") != BASE_MODEL_TYPE or
            config.get("text_config", {}).get("model_type") != "qwen3_5_text"):
        fail("Expected full original Qwen3.5 4B multimodal HF checkpoint")
    hidden = config["text_config"].get("hidden_size")
    if hidden != 2560:
        fail("This first-pass recipe is ONLY for the 4B variant")
    tokenizer = require_file(root / "tokenizer.json", 32 * 1024 * 1024)
    token_cfg = require_file(root / "tokenizer_config.json", 8 * 1024 * 1024)
    # Sanity check that actual local shards are present, not just config.json.
    idx = root / "model.safetensors.index.json"
    if idx.is_symlink():
        fail("Model index cannot be a symlink")
    if idx.is_file():
        mapping = read_json(idx, 8 * 1024 * 1024).get("weight_map")
        if not isinstance(mapping, dict) or not mapping:
            fail("Malformed model.safetensors.index.json")
        names = set(mapping.values())
        if (any(not isinstance(n, str) or not re.fullmatch(
                r"[A-Za-z0-9_.-]+\.safetensors", n) for n in names)
                or len(names) > 256):
            fail("Unsafe shard name in model index")
        shards = [root / name for name in sorted(names)]
    else:
        shards = [root / "model.safetensors"]
    for shard in shards:
        require_file(shard, 32 * 1024 * 1024 * 1024)
    if sum(p.stat().st_size for p in shards) < 256 * 1024 * 1024:
        fail("Model files too small to be a real 4B checkpoint")
    # Compute actual SHA-256, not file sizes alone. Safetensors shards can be
    # silently replaced with identically sized bytes between prep and train.
    # This one-time preflight may take time on a full HF checkpoint.
    shard_checksums = [{"name": p.name, "bytes": p.stat().st_size,
                        "sha256": digest(p)} for p in shards]
    return {"path": str(root), "config_sha256": digest(root / "config.json"),
            "tokenizer_sha256": digest(tokenizer),
            "tokenizer_config_sha256": digest(token_cfg),
            "shard_count": len(shards),
            "shard_sizes": [p.stat().st_size for p in shards],
            "shards": shard_checksums,
            "index_sha256": digest(idx) if idx.is_file() else None}

def yaml_string(value):
    # JSON double-quoted strings are also YAML-safe. This avoids path/config
    # injection and correctly escapes Windows and WSL file paths.
    return json.dumps(str(value), ensure_ascii=False)

def build_yaml(base, data, output, sequence_len, epochs):
    model = yaml_string(base["path"])
    train = yaml_string(data["train"]["path"])
    out = yaml_string(str(output))
    prepared = yaml_string(str(output / "prepared"))
    # Match official Axolotl Qwen3.5 text-only recipes. No sample-packing:
    # linear-attention packing requires extra compatible FLA kernels.
    # WSL2 Linux with CUDA is the intended training environment. Packaging
    # and Windows app runtime remain C++/Qt, not Python.
    return f"""# Generated by EmberMind offline QLoRA preflight. Experimental.
# NO online Hugging Face model or training data IDs. DO NOT upload this file.
base_model: {model}
chat_template: qwen3_5
datasets:
  - path: {train}
    type: chat_template
    field_messages: messages
val_set_size: 0.0
output_dir: {out}
dataset_prepared_path: {prepared}
sequence_len: {sequence_len}
sample_packing: false
load_in_4bit: true
adapter: qlora
lora_r: 16
lora_alpha: 32
lora_dropout: 0.05
lora_target_modules:
  - q_proj
  - k_proj
  - v_proj
  - o_proj
  - down_proj
  - up_proj
micro_batch_size: 1
gradient_accumulation_steps: 8
num_epochs: {epochs}
optimizer: adamw_torch_4bit
lr_scheduler: cosine
learning_rate: 0.00005
bf16: auto
tf32: true
activation_checkpointing:
  enabled: true
  reentrant: false
logging_steps: 1
warmup_ratio: 0.05
evals_per_epoch: 0
saves_per_epoch: 1
weight_decay: 0.0
wandb_project:
wandb_entity:
wandb_watch:
wandb_name:
wandb_log_model:
"""

def write_exclusive(path, data):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                 getattr(os, "O_NOFOLLOW", 0), 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(data)
        f.flush()
        os.fsync(f.fileno())

def prepare(base_dir, dataset_dir, output_dir, revision, sequence_len, epochs):
    if not isinstance(revision, str) or not REVISION.fullmatch(revision):
        fail("Pin exact 40-hex base model commit revision")
    if sequence_len not in (512, 1024, 1536, 2048):
        fail("Only small sequence lengths are permitted on first-pass 12GB tests")
    if epochs not in (1, 2, 3):
        fail("Limit first-pass runs to 1..3 epochs")
    base = inspect_base(base_dir)
    data = inspect_dataset(dataset_dir)
    out = Path(output_dir)
    if out.exists() or out.is_symlink() or not out.parent.is_dir():
        fail("Output must be a new folder under an existing local parent")
    # Resolve only after parent exists; require separation from source files.
    root = out.resolve()
    sources = (Path(base["path"]), Path(dataset_dir).resolve())
    if any(root == p or p in root.parents or root in p.parents for p in sources):
        fail("Training output must not contain or overlap model/dataset source")
    config = build_yaml(base, data, root, sequence_len, epochs)
    record = {
        "schema": SCHEMA, "base_revision_asserted_by_user": revision,
        "base": base, "dataset": data, "config_sha256": hashlib.sha256(
            config.encode("utf-8")).hexdigest(),
        "sequence_len": sequence_len, "epochs": epochs,
        "training_performed": False,
        "note": ("Private local paths. Source revision and license are operator "
                 "assertions. Software compatibility and RTX 4070 12GB VRAM use "
                 "MUST be tested before calling this a working recipe. "
                 "No training run or model download occurred.")
    }
    out.mkdir(mode=0o700)
    try:
        write_exclusive(out / "axolotl.yaml", config.encode("utf-8"))
        write_exclusive(out / "ember_training_lock.json",
                        (json.dumps(record, indent=2) + "\n").encode("utf-8"))
    except Exception:
        for path in (out / "axolotl.yaml", out / "ember_training_lock.json"):
            if path.is_file() and not path.is_symlink():
                path.unlink()
        out.rmdir()
        raise
    return record

def run_once(run_dir, explicitly_approved):
    if not explicitly_approved:
        fail("Training requires explicit --i-approve-one-shot-training")
    root = require_dir(run_dir)
    lock = read_json(root / "ember_training_lock.json", 16384)
    if lock.get("schema") != SCHEMA:
        fail("Missing local Ember training lock")
    config = require_file(root / "axolotl.yaml", 16384)
    if digest(config) != lock.get("config_sha256"):
        fail("Axolotl config changed after private preflight")
    source = lock.get("dataset")
    if not isinstance(source, dict):
        fail("Missing dataset in training lock")
    for key in ("train", "eval"):
        entry = source.get(key)
        if not isinstance(entry, dict) or digest(require_file(
                entry.get("path"), DATA_LIMIT)) != entry.get("sha256"):
            fail("Dataset changed after training preflight")
    base = lock.get("base")
    if not isinstance(base, dict) or inspect_base(base.get("path")) != base:
        fail("Local base metadata/weights changed since preflight")
    executable = shutil.which("axolotl")
    if not executable:
        fail("Axolotl is not installed in this local training environment")
    env = dict(os.environ)
    env.update({
        "HF_HUB_OFFLINE": "1", "TRANSFORMERS_OFFLINE": "1",
        "HF_DATASETS_OFFLINE": "1", "WANDB_MODE": "disabled",
        "HF_HUB_DISABLE_TELEMETRY": "1", "DO_NOT_TRACK": "1",
        "TOKENIZERS_PARALLELISM": "false"
    })
    print("Launching one user-requested offline Axolotl run; no auto-downloads.",
          flush=True)
    return subprocess.run([executable, "train", str(config)], env=env,
                          cwd=str(root), check=False).returncode

def self_test():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        base, data = root / "base", root / "data"
        base.mkdir()
        data.mkdir()
        (base / "config.json").write_text(json.dumps({
            "model_type": "qwen3_5",
            "text_config": {"model_type": "qwen3_5_text",
                            "hidden_size": 2560}}))
        (base / "tokenizer.json").write_text("{}")
        (base / "tokenizer_config.json").write_text("{}")
        # Sparse synthetic blob; no model is downloaded or loaded.
        with (base / "model.safetensors").open("wb") as f:
            f.truncate(257 * 1024 * 1024)
        report = {"schema": SPLIT_SCHEMA}
        for key in ("train", "eval"):
            blob = (json.dumps({"messages": [
                {"role": "user", "content": "Test " + key},
                {"role": "assistant", "content": "Synthetic only"}]}) + "\n")
            (data / (key + ".jsonl")).write_text(blob)
            report[key + "_sha256"] = digest(data / (key + ".jsonl"))
            report[key + "_examples"] = 1
        (data / "report.json").write_text(json.dumps(report))
        target = root / "run1"
        x = prepare(base, data, target, "a" * 40, 512, 1)
        assert x["schema"] == SCHEMA
        text = (target / "axolotl.yaml").read_text()
        assert "adapter: qlora" in text and "sample_packing: false" in text
        # JSON double-quoted YAML escapes Windows backslashes; compare the
        # serialized value instead of its unescaped filesystem spelling.
        # Assert the exact paths emitted by the preflight record. On Windows,
        # temporary paths may have 8.3 aliases and YAML escapes backslashes.
        assert yaml_string(x["base"]["path"]) in text
        assert yaml_string(x["dataset"]["train"]["path"]) in text
        assert yaml_string(x["dataset"]["eval"]["path"]) not in text
        try:
            prepare(base, data, target, "a" * 40, 512, 1)
            raise AssertionError("Existing training config overwritten")
        except ValueError:
            pass
        try:
            run_once(target, False)
            raise AssertionError("Unapproved training launch allowed")
        except ValueError:
            pass
        # Simulate replacement of one shard without changing its byte count.
        # A size-only fingerprint must never authorize the training launch.
        with (base / "model.safetensors").open("r+b") as f:
            f.write(b"changed-with-same-size")
        assert inspect_base(base)["shards"] != x["base"]["shards"]
        try:
            run_once(target, True)
            raise AssertionError("Same-size changed model weights accepted")
        except ValueError:
            pass
        (data / "train.jsonl").write_text('{"messages":[]}\n')
        try:
            prepare(base, data, root / "tampered", "a" * 40, 512, 1)
            raise AssertionError("Tampered dataset accepted")
        except ValueError:
            pass
    print("EmberMind QLoRA generation self-tests PASS (offline, zero training)")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("prepare", help="Write local Axolotl recipe after checks")
    p.add_argument("--base-dir", required=True, type=Path)
    p.add_argument("--dataset-dir", required=True, type=Path)
    p.add_argument("--output-dir", required=True, type=Path)
    p.add_argument("--base-revision", required=True)
    p.add_argument("--sequence-len", type=int, choices=(512, 1024, 1536, 2048),
                   default=1024)
    p.add_argument("--epochs", type=int, choices=(1, 2, 3), default=1)
    run = sub.add_parser("train", help="Manually start ONE offline trainer process")
    run.add_argument("--run-dir", type=Path, required=True)
    run.add_argument("--i-approve-one-shot-training", action="store_true")
    sub.add_parser("self-test", help="No downloads, GPU, data or training")
    args = parser.parse_args()
    try:
        if args.command == "self-test":
            self_test()
        elif args.command == "prepare":
            print(json.dumps(prepare(args.base_dir, args.dataset_dir,
                                     args.output_dir, args.base_revision,
                                     args.sequence_len, args.epochs),
                             indent=2))
        else:
            raise SystemExit(run_once(args.run_dir, args.i_approve_one_shot_training))
    except (ValueError, OSError, TypeError, UnicodeError) as exc:
        parser.error(str(exc))

if __name__ == "__main__":
    main()
