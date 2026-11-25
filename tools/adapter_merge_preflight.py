#!/usr/bin/env python3
"""Verify local LoRA adapter provenance and basic compatibility before merging.

A preflight PASS is NOT permission to distribute model weights, nor proof that
a merge preserves quality. No model loading, merging, training, network or
background jobs. Files stay on the local machine.
"""
import argparse
import hashlib
import json
import os
import re
import tempfile
from pathlib import Path

SCHEMA = "ember-adapter-lock-v1"
TASKS = {"conversation", "coding", "troubleshooting", "story", "roleplay", "tool-use"}
SHA256 = re.compile(r"[0-9a-f]{64}\Z")
REVISION = re.compile(r"[0-9a-f]{40}\Z")
SIMPLE_ID = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+\Z")
MAX_JSON = 256 * 1024
ADAPTER_WEIGHT = "adapter_model.safetensors"
LOCK_NAME = "ember_adapter_lock.json"

def reject(message):
    raise ValueError(message)

def read_json(path, limit=MAX_JSON):
    path = Path(path)
    if not path.is_file() or path.is_symlink() or not 0 < path.stat().st_size <= limit:
        reject("Missing, symlinked or excessive metadata: " + path.name)
    try:
        obj = json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeError, json.JSONDecodeError):
        reject("Invalid JSON metadata: " + path.name)
    if not isinstance(obj, dict):
        reject("Metadata must be a JSON object")
    return obj

def digest(path):
    path = Path(path)
    if not path.is_file() or path.is_symlink():
        reject("Missing or symlinked local file: " + path.name)
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(4 * 1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def canonical(obj):
    return json.dumps(obj, sort_keys=True, separators=(",", ":")).encode("utf-8")

def signature(adapter_config):
    if adapter_config.get("peft_type") != "LORA":
        reject("Only ordinary PEFT LoRA adapters can be preflighted")
    targets = adapter_config.get("target_modules")
    if (not isinstance(targets, list) or not targets
            or any(not isinstance(x, str) or not x or len(x) > 100 for x in targets)
            or len(targets) != len(set(targets))):
        reject("LoRA target_modules must be explicit unique module names")
    rank, alpha = adapter_config.get("r"), adapter_config.get("lora_alpha")
    if (isinstance(rank, bool) or not isinstance(rank, int) or not 1 <= rank <= 512
            or isinstance(alpha, bool) or not isinstance(alpha, (int, float))
            or not 0 < alpha <= 4096):
        reject("Invalid LoRA rank or scaling")
    if (adapter_config.get("rank_pattern") or adapter_config.get("alpha_pattern")
            or adapter_config.get("modules_to_save") or adapter_config.get("use_dora")
            or adapter_config.get("use_rslora") or adapter_config.get("layer_replication")):
        reject("Advanced/reparameterized LoRA layouts need separate validation")
    if adapter_config.get("bias", "none") != "none":
        reject("LoRA bias updates require separate validation")
    return {
        "peft_type": "LORA",
        "target_modules": sorted(targets),
        "r": rank, "lora_alpha": alpha,
        "fan_in_fan_out": adapter_config.get("fan_in_fan_out", False)
    }

def adapter_files(directory):
    root = Path(directory)
    if not root.is_dir() or root.is_symlink():
        reject("Adapter directory must be a real local directory")
    config_path = root / "adapter_config.json"
    weights_path = root / ADAPTER_WEIGHT
    config = read_json(config_path)
    sig = signature(config)
    weights_sha = digest(weights_path)
    return root, config, sig, digest(config_path), weights_sha

def lock(adapter, base_id, base_revision, base_config, tokenizer, dataset_report,
         task, license_name):
    if not isinstance(base_id, str) or not SIMPLE_ID.fullmatch(base_id):
        reject("Base identity must use organization/repository form")
    if not REVISION.fullmatch(base_revision):
        reject("Base revision must be a pinned 40-character hexadecimal Git commit")
    if task not in TASKS:
        reject("Unsupported target task")
    if (not license_name or not 1 <= len(license_name) <= 120
            or any(ord(x) < 32 for x in license_name)):
        reject("License identifier required (not independently verified)")
    root, config, sig, config_sha, weights_sha = adapter_files(adapter)
    declared = config.get("base_model_name_or_path")
    if not isinstance(declared, str) or not declared.strip():
        reject("Adapter must declare a base model")
    if SIMPLE_ID.fullmatch(declared) and declared != base_id:
        reject("Adapter declares a different base repository")
    model_cfg = read_json(base_config)
    if not isinstance(model_cfg.get("model_type"), str):
        reject("Base model config lacks model_type")
    token_path = Path(tokenizer)
    dataset = read_json(dataset_report)
    if (dataset.get("schema") != "ember-mind-curation-report-v1"
            or not isinstance(dataset.get("train_sha256"), str)
            or not SHA256.fullmatch(dataset["train_sha256"])):
        reject("Dataset report must come from EmberMind Studio")
    output = root / LOCK_NAME
    record = {
        "schema": SCHEMA, "base_id": base_id,
        "base_revision": base_revision,
        "base_model_type": model_cfg["model_type"],
        "base_config_sha256": digest(base_config),
        "tokenizer_sha256": digest(token_path),
        "adapter_config_sha256": config_sha,
        "adapter_weights_sha256": weights_sha,
        "adapter_signature": sig,
        "training_split_sha256": dataset["train_sha256"],
        "task": task, "license_declaration": license_name,
        "provenance_asserted_by_operator": True,
        "training_verified": False
    }
    payload = json.dumps(record, ensure_ascii=False, indent=2).encode("utf-8") + b"\n"
    # Exclusive create; never overwrite a lock (recreate only by an intentional
    # operator action). Avoid making a private artifact world-readable on POSIX.
    flags = os.O_CREAT | os.O_EXCL | os.O_WRONLY | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(output, flags, 0o600), "wb") as f:
        f.write(payload)
        f.flush()
        os.fsync(f.fileno())
    return record

def check(directories):
    if not 2 <= len(directories) <= 8:
        reject("Compare between two and eight manually supplied adapters")
    locks, seen_paths = [], set()
    for directory in directories:
        root, _, sig, config_sha, weights_sha = adapter_files(directory)
        absolute = str(root.resolve())
        if absolute in seen_paths:
            reject("Same adapter supplied multiple times")
        seen_paths.add(absolute)
        record = read_json(root / LOCK_NAME, 32 * 1024)
        if record.get("schema") != SCHEMA:
            reject("Missing compatible Ember adapter lock")
        if (record.get("adapter_config_sha256") != config_sha
                or record.get("adapter_weights_sha256") != weights_sha
                or record.get("adapter_signature") != sig):
            reject("Adapter files changed after provenance lock was written")
        if record.get("provenance_asserted_by_operator") is not True:
            reject("Adapter provenance was not operator asserted")
        for name, pattern in (
            ("base_revision", REVISION),
            ("base_config_sha256", SHA256),
            ("tokenizer_sha256", SHA256),
            ("training_split_sha256", SHA256)):
            value = record.get(name)
            if not isinstance(value, str) or not pattern.fullmatch(value):
                reject("Missing/invalid pinned adapter provenance: " + name)
        locks.append(record)
    common = ("base_id", "base_revision", "base_model_type",
              "base_config_sha256", "tokenizer_sha256", "adapter_signature")
    first = locks[0]
    for i, item in enumerate(locks[1:], 2):
        for field in common:
            if item.get(field) != first.get(field):
                reject("Adapter " + str(i) + " incompatible: " + field)
    return {
        "schema": "ember-adapter-preflight-v1",
        "compatible_metadata": True, "count": len(locks),
        "base_id": first["base_id"], "base_revision": first["base_revision"],
        "tasks": [r["task"] for r in locks],
        "adapter_sha256": [r["adapter_weights_sha256"] for r in locks],
        "note": ("Metadata/digest compatibility ONLY. Base revision and dataset/license "
                 "were asserted by an operator, not independently validated; no "
                 "weight tensor parsing, model merge, training, quality or privacy "
                 "review has occurred. Test merged weights separately.")
    }

def self_test():
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        config = root / "config.json"
        tokenizer = root / "tokenizer.json"
        dataset = root / "report.json"
        config.write_text('{"model_type":"qwen3_5","hidden_size":256}', encoding="utf-8")
        tokenizer.write_text('{"model":{"type":"BPE"}}', encoding="utf-8")
        dataset.write_text(json.dumps({
            "schema": "ember-mind-curation-report-v1",
            "train_sha256": "a" * 64}), encoding="utf-8")
        adapters = []
        for i, task in enumerate(("conversation", "coding")):
            folder = root / ("adapter-" + str(i))
            folder.mkdir()
            (folder / "adapter_config.json").write_text(json.dumps({
                "peft_type": "LORA", "target_modules": ["q_proj", "v_proj"],
                "r": 8, "lora_alpha": 16, "bias": "none",
                "base_model_name_or_path": "Qwen/Qwen3.5-4B"
            }), encoding="utf-8")
            (folder / ADAPTER_WEIGHT).write_bytes(("synthetic " + task).encode())
            lock(folder, "Qwen/Qwen3.5-4B", "a" * 40, config,
                 tokenizer, dataset, task, "Apache-2.0")
            adapters.append(folder)
        result = check(adapters)
        assert result["compatible_metadata"] and result["count"] == 2
        try:
            lock(adapters[0], "Qwen/Qwen3.5-4B", "a" * 40,
                 config, tokenizer, dataset, "conversation", "Apache-2.0")
            raise AssertionError("Lock unexpectedly overwritten")
        except FileExistsError:
            pass
        # Changing a tensor blob or its LoRA configuration invalidates the lock.
        (adapters[1] / ADAPTER_WEIGHT).write_bytes(b"changed synthetic weights")
        try:
            check(adapters)
            raise AssertionError("Modified adapter accepted")
        except ValueError:
            pass
        # Even valid locks from different model revisions may not be mixed.
        (adapters[1] / ADAPTER_WEIGHT).write_bytes(b"synthetic coding")
        other_lock = adapters[1] / LOCK_NAME
        record = read_json(other_lock)
        record["base_revision"] = "b" * 40
        other_lock.write_text(json.dumps(record), encoding="utf-8")
        try:
            check(adapters)
            raise AssertionError("Different model revisions accepted")
        except ValueError:
            pass
        # Mixed model architecture/model size must fail even with valid hashes.
        record["base_revision"] = "a" * 40
        record["base_config_sha256"] = "b" * 64
        other_lock.write_text(json.dumps(record), encoding="utf-8")
        try:
            check(adapters)
            raise AssertionError("Different model configurations accepted")
        except ValueError:
            pass
    print("Ember adapter provenance and merge-preflight self-tests passed (synthetic only)")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    create = sub.add_parser("lock", help="Pin provenance of a locally trained LoRA adapter")
    create.add_argument("--adapter", required=True, type=Path)
    create.add_argument("--base-id", required=True)
    create.add_argument("--base-revision", required=True)
    create.add_argument("--base-config", required=True, type=Path)
    create.add_argument("--tokenizer", required=True, type=Path)
    create.add_argument("--dataset-report", required=True, type=Path)
    create.add_argument("--task", required=True, choices=sorted(TASKS))
    create.add_argument("--license", required=True)
    compare = sub.add_parser("check", help="Compare pinned adapters before attempting a merge")
    compare.add_argument("--adapter", action="append", type=Path, required=True)
    sub.add_parser("self-test", help="Run synthetic checks with no models or network")
    args = parser.parse_args()
    try:
        if args.command == "self-test":
            self_test()
        elif args.command == "lock":
            print(json.dumps(lock(args.adapter, args.base_id, args.base_revision,
                                  args.base_config, args.tokenizer,
                                  args.dataset_report, args.task, args.license),
                             indent=2))
        else:
            print(json.dumps(check(args.adapter), indent=2))
    except (OSError, ValueError, UnicodeError) as exc:
        parser.error(str(exc))

if __name__ == "__main__":
    main()
