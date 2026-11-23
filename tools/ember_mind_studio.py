#!/usr/bin/env python3
"""Prepare private, group-separated SFT datasets for a future custom EmberMind.

No training, network, model downloads, scheduled jobs, or prompt uploads.
Only synthetic built-in data is used during self-test.
"""
import argparse
import collections
import hashlib
import json
import math
import os
import re
import tempfile
import unicodedata
from pathlib import Path

SCHEMA = "ember-mind-sft-v1"
TASKS = {"conversation", "coding", "troubleshooting", "story", "roleplay", "tool-use"}
LANGUAGES = {"en", "ru", "mixed"}
RIGHTS = {"user-authored", "licensed", "permissive"}
SPLIT_SALT = "ember-mind-eval-holdout-v1"

def fail(message):
    raise ValueError(message)

def sha(data):
    return hashlib.sha256(data).hexdigest()

def canonical(obj):
    return json.dumps(obj, ensure_ascii=False, sort_keys=True,
                      separators=(",", ":")).encode("utf-8")

def short_string(value, field, limit=100):
    if (not isinstance(value, str) or not 1 <= len(value) <= limit or
            any(ord(c) < 32 for c in value)):
        fail(field + " must be a short printable string")
    return value

def validate_example(item):
    allowed = {"schema", "sample_id", "group_id", "task", "language",
               "rights", "license", "messages"}
    if not isinstance(item, dict) or set(item) - allowed or item.get("schema") != SCHEMA:
        fail("Unsupported example schema or field")
    short_string(item.get("sample_id"), "sample_id")
    short_string(item.get("group_id"), "group_id")
    for name, allowed_values in (("task", TASKS), ("language", LANGUAGES),
                                 ("rights", RIGHTS)):
        value = item.get(name)
        if not isinstance(value, str) or value not in allowed_values:
            fail("Unsupported task, language or rights declaration")
    license_name = item.get("license", "")
    if item["rights"] == "user-authored":
        if license_name not in ("", None):
            fail("User-authored samples must not name an external license")
    else:
        short_string(license_name, "license", 160)
    messages = item.get("messages")
    if not isinstance(messages, list) or not 2 <= len(messages) <= 32:
        fail("Samples need 2..32 messages")
    expected = "user"
    for i, msg in enumerate(messages):
        if not isinstance(msg, dict) or set(msg) != {"role", "content"}:
            fail("Message must contain only role and content")
        role, content = msg["role"], msg["content"]
        if i == 0 and role == "system":
            pass
        elif role == expected:
            expected = "assistant" if expected == "user" else "user"
        else:
            fail("User/assistant turns must alternate")
        if (not isinstance(content, str) or not content.strip()
                or len(content) > 12000 or "\x00" in content):
            fail("Missing, excessive or invalid message text")
    if messages[-1]["role"] != "assistant":
        fail("Sample must end with an assistant response")
    return item

def normalized_user_turns(messages):
    """Case/Unicode/whitespace-normalized user prompts for leakage checks.

    A duplicated user-side exchange across different groups is not an
    independent evaluation example even if its assistant completion differs.
    Normalization is used for fingerprints ONLY, never to modify training text.
    """
    if not isinstance(messages, list):
        fail("Messages must be a list")
    prompts = []
    for message in messages:
        if isinstance(message, dict) and message.get("role") == "user":
            text = unicodedata.normalize("NFKC", message["content"])
            prompts.append(re.sub(r"\\s+", " ", text).strip().casefold())
    if not prompts:
        fail("Example has no user turns")
    return sha(canonical(prompts))


def unique_json_keys(pairs):
    result = {}
    for name, value in pairs:
        if name in result:
            fail("Duplicate JSON key in training example")
        result[name] = value
    return result

def read_examples(path):
    path = Path(path)
    if (not path.is_file() or path.is_symlink() or
            not 0 < path.stat().st_size <= 32 * 1024 * 1024):
        fail("Input must be a real local JSONL file <= 32 MiB")
    rows, ids, contents, group_tasks = [], set(), set(), {}
    prompt_groups = {}
    with path.open(encoding="utf-8-sig") as f:
        for i, line in enumerate(f, 1):
            if i > 20000 or not line.strip() or len(line) > 512 * 1024:
                fail("Blank, excessive or too many JSONL examples")
            try:
                obj = validate_example(json.loads(
                    line, object_pairs_hook=unique_json_keys,
                    parse_constant=lambda value: fail("Non-finite JSON number")))
            except json.JSONDecodeError:
                fail("Malformed JSONL example")
            sample_id, group, task = obj["sample_id"], obj["group_id"], obj["task"]
            content_fp = sha(canonical(obj["messages"]))
            prompt_fp = normalized_user_turns(obj["messages"])
            earlier = prompt_groups.setdefault(prompt_fp, group)
            if earlier != group:
                fail("Same normalized user prompts found in distinct "
                     "conversation groups: independent eval is unsafe")
            if sample_id in ids or content_fp in contents:
                fail("Duplicate sample ID or message sequence")
            ids.add(sample_id)
            contents.add(content_fp)
            old = group_tasks.setdefault(group, task)
            if old != task:
                fail("Conversation group cannot mix tasks")
            rows.append(obj)
    if not rows:
        fail("Empty dataset")
    return rows

def holdout(rows, fraction):
    if not math.isfinite(fraction) or not 0.05 <= fraction <= 0.40:
        fail("Evaluation fraction must be 0.05..0.40")
    groups = collections.defaultdict(set)
    for row in rows:
        groups[row["task"]].add(row["group_id"])
    eval_groups = set()
    for task, names in groups.items():
        if len(names) < 2:
            fail("Each task requires two or more independent conversation groups")
        ordered = sorted(names, key=lambda group: sha(
            (SPLIT_SALT + "\x00" + task + "\x00" + group).encode("utf-8")))
        count = max(1, min(len(ordered) - 1, round(len(ordered) * fraction)))
        eval_groups.update(ordered[:count])
    return eval_groups

def private_write(path, payload):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(payload)
        f.flush()
        os.fsync(f.fileno())

def prepare(input_path, output_dir, fraction):
    rows = read_examples(input_path)
    eval_groups = holdout(rows, fraction)
    out = Path(output_dir)
    if out.exists() or out.is_symlink():
        fail("Output directory exists: refusing to overwrite")
    if not out.parent.is_dir() or out.parent.is_symlink():
        fail("Create a real local output parent directory first")
    buckets = {"train": [], "eval": []}
    counts = {"train": collections.Counter(), "eval": collections.Counter()}
    for row in rows:
        bucket = "eval" if row["group_id"] in eval_groups else "train"
        buckets[bucket].append(canonical({"messages": row["messages"]}) + b"\n")
        counts[bucket][row["task"]] += 1
    if not buckets["train"] or not buckets["eval"]:
        fail("No train/eval examples")
    blobs = {name: b"".join(data) for name, data in buckets.items()}
    summary = {
        "schema": "ember-mind-curation-report-v1",
        "input_sha256": sha(Path(input_path).read_bytes()),
        "split_seed": SPLIT_SALT,
        "eval_fraction": fraction,
        "train_examples": len(buckets["train"]),
        "eval_examples": len(buckets["eval"]),
        "train_task_counts": dict(sorted(counts["train"].items())),
        "eval_task_counts": dict(sorted(counts["eval"].items())),
        "train_sha256": sha(blobs["train"]),
        "eval_sha256": sha(blobs["eval"]),
        "training_performed": False,
        "model_weights_created": False,
        "notice": ("Train/eval files hold plaintext examples. Keep private. "
                   "Rights declarations need independent review before any distribution. "
                   "This summary contains no conversation text or source IDs.")
    }
    out.mkdir(mode=0o700)
    try:
        private_write(out / "train.jsonl", blobs["train"])
        private_write(out / "eval.jsonl", blobs["eval"])
        private_write(out / "report.json",
                      json.dumps(summary, ensure_ascii=False, indent=2).encode("utf-8") + b"\n")
    except Exception:
        for name in ("train.jsonl", "eval.jsonl", "report.json"):
            candidate = out / name
            if candidate.is_file() and not candidate.is_symlink():
                candidate.unlink()
        out.rmdir()
        raise
    return summary

def self_test():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        marker = "CONFIDENTIAL_SYNTHETIC_TEST_SENTINEL"
        examples = []
        for task in ("conversation", "coding", "story"):
            for i in range(4):
                examples.append({
                    "schema": SCHEMA, "sample_id": f"{task}-{i}",
                    "group_id": f"{task}-group-{i}", "task": task,
                    "language": "en", "rights": "user-authored",
                    "messages": [{"role": "user", "content": f"{task} example {i}"},
                                 {"role": "assistant", "content": marker + task + str(i)}]})
        source = root / "source.jsonl"
        source.write_text("".join(json.dumps(x) + "\n" for x in examples), encoding="utf-8")
        a = prepare(source, root / "a", 0.25)
        b = prepare(source, root / "b", 0.25)
        assert a["train_sha256"] == b["train_sha256"]
        assert a["eval_sha256"] == b["eval_sha256"]
        assert a["train_examples"] + a["eval_examples"] == 12
        assert len(a["eval_task_counts"]) == 3
        assert marker not in (root / "a" / "report.json").read_text()
        assert marker in (root / "a" / "train.jsonl").read_text()
        try:
            prepare(source, root / "a", 0.25)
            raise AssertionError("Overwrite accepted")
        except ValueError:
            pass
        duplicate = root / "duplicate.jsonl"
        duplicate.write_text(source.read_text() + json.dumps(examples[0]) + "\n")
        try:
            prepare(duplicate, root / "duplicate-out", 0.25)
            raise AssertionError("Duplicate accepted")
        except ValueError:
            assert not (root / "duplicate-out").exists()
        # Same user-facing question with different assistant completions
        # is still train/eval leakage when groups can split independently.
        repeated = dict(examples[0], sample_id="reworded-answer",
                        group_id="other-group",
                        messages=[
                            {"role": "user",
                             "content": "  " + examples[0]["messages"][0]["content"].upper() + "   "},
                            {"role": "assistant",
                             "content": "A different synthetic answer."}])
        leaked = root / "cross-group-prompt.jsonl"
        leaked.write_text(source.read_text(encoding="utf-8") +
                          json.dumps(repeated) + "\\n", encoding="utf-8")
        try:
            read_examples(leaked)
            raise AssertionError("Cross-group normalized prompt leak accepted")
        except ValueError:
            pass
        # Normalization must never rewrite the user's actual training text.
        assert normalized_user_turns(examples[0]["messages"]) == (
            normalized_user_turns(repeated["messages"]))
        for field in ("task", "language", "rights"):
            bad_unhashable = dict(examples[0], **{field: []})
            try:
                validate_example(bad_unhashable)
                raise AssertionError("Unhashable metadata was accepted")
            except ValueError:
                pass
        duplicate_keys = root / "duplicate-keys.jsonl"
        duplicate_keys.write_text(source.read_text().replace(
            '"schema": ', '"schema": "invalid", "schema": ', 1), encoding="utf-8")
        try:
            read_examples(duplicate_keys)
            raise AssertionError("Duplicate JSON keys accepted")
        except ValueError:
            pass
        bad = dict(examples[0], rights="arbitrary-scrape")
        try:
            validate_example(bad)
            raise AssertionError("Unapproved rights declaration accepted")
        except ValueError:
            pass
        bad = dict(examples[0], messages=[{"role": "assistant", "content": "Wrong"}])
        try:
            validate_example(bad)
            raise AssertionError("Invalid role order accepted")
        except ValueError:
            pass
    # The existing explicit local model-lab test entrypoint also exercises the
    # one-shot training *preparation* and consented voice corpus boundaries.
    # Neither test runs a trainer, downloads weights, or uses the network.
    import ember_mind_qlora
    import ember_voice_corpus
    ember_mind_qlora.self_test()
    ember_voice_corpus.self_test()
    print("EmberMind + Ember Voice model preparation self-tests passed offline")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--input", required=True, type=Path)
    prep.add_argument("--output-dir", required=True, type=Path)
    prep.add_argument("--eval-fraction", type=float, default=0.20)
    sub.add_parser("self-test")
    args = parser.parse_args()
    try:
        if args.command == "self-test":
            self_test()
        else:
            print(json.dumps(prepare(args.input, args.output_dir, args.eval_fraction),
                             ensure_ascii=False, indent=2))
    except (OSError, ValueError, UnicodeError) as exc:
        parser.error(str(exc))

if __name__ == "__main__":
    main()
