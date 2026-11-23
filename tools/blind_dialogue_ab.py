#!/usr/bin/env python3
"""On-demand, localhost-only blind evaluation of multi-turn EmberMind dialogue.

A companion to blind_text_ab.py: runs original synthetic conversations, never
reads personal chats, and compares complete multi-turn trajectories. No models,
weights, training, uploads, or timed/background tasks are started by this tool.
"""
import argparse
import hashlib
import json
import math
import random
import re
import time
from pathlib import Path
from urllib.request import Request

import blind_text_ab as single

SCHEMA = "ember-dialogue-ab-capture-v1"
SCORE_SCHEMA = "ember-dialogue-ab-score-v1"
# Every user turn and instruction is original, non-private test material.
SCENARIOS = (
    ("empathy-repair-en",
     "Talk like a thoughtful conversational partner. Avoid canned reassurances. "
     "Track corrections and ask only genuinely useful follow-ups.",
     ("I finally finished fixing a bug after hours, but I feel strangely flat.",
      "It's not that I need encouragement. I'm just too tired to enjoy finishing it.",
      "Exactly. Could you give me one specific idea for this evening, not a list?")),
    ("conversation-ru",
     "Веди живой разговор на русском, без официоза, шаблонной поддержки и повторов. "
     "Сохраняй контекст и учитывай уточнения.",
     ("Я хотел сегодня позаниматься проектом, но целый вечер разбирался с ошибкой.",
      "Не, ошибку я уже решил. Хочется просто немного переключиться.",
      "Давай одну конкретную идею, а не целую лекцию.")),
    ("collaborative-fiction",
     "Co-write a restrained Gothic story with believable dialogue. "
     "Keep established facts and do not control the player's character.",
     ("Mara is afraid of deep water; Leon cannot read. Their lantern is nearly out. "
      "Open a scene in the flooded bell tower, ending at a decision point.",
      "Mara stays on the dry landing. Leon finds a letter in an iron box; show what happens next.",
      "I decide Mara will not enter the water. Keep Leon's illiteracy and let me decide her next move.")),
    ("debugging-followup",
     "Collaborate on troubleshooting. Do not claim access to files, logs or test results.",
     ("A C++ Qt app freezes when its output audio device is unplugged. Where would you look first?",
      "The audio sink has a queued callback which can run after its owner is destroyed.",
      "Suggest one minimal defensive change and a regression test, but don't claim you ran it.")),
    ("instruction-revision",
     "Follow the current user's correction over earlier assumptions. Respond concisely.",
     ("I want a short explanation of how a SQL index helps searches.",
      "Correction: I already know what an index is. Explain a trade-off of having too many.",
      "Make the answer one sentence and mention writes explicitly.")),
)
SUITE_HASH = hashlib.sha256(json.dumps(SCENARIOS, ensure_ascii=False,
        separators=(",", ":")).encode("utf-8")).hexdigest()
MAX_CAPTURE = 256 * 1024
MAX_ANSWER = 12000
THOUGHT_MARKER = re.compile(r"<\s*/?\s*think\b", re.IGNORECASE)


def request_messages(system, turns, temperature, top_p, seed):
    if not isinstance(turns, list) or not turns or len(turns) > 6:
        raise ValueError("Invalid dialogue turns")
    messages = [{"role": "system", "content":
        "You are Ember, a locally running assistant. " + system}]
    expected = "user"
    for role, text in turns:
        if role != expected or not isinstance(text, str) or not text.strip():
            raise ValueError("Invalid dialogue role order or message")
        messages.append({"role": role, "content": text})
        expected = "assistant" if expected == "user" else "user"
    if messages[-1]["role"] != "user":
        raise ValueError("A dialogue request must end in a user message")
    return {"model": "ember", "messages": messages, "stream": False,
            "temperature": temperature, "top_p": top_p, "seed": seed,
            "max_tokens": 384,
            "chat_template_kwargs": {"enable_thinking": False}}


def request_once(system, turns, temperature, top_p, seed):
    single.validate_endpoint(single.ENDPOINT)
    payload = request_messages(system, turns, temperature, top_p, seed)
    body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    req = Request(single.ENDPOINT, data=body, method="POST", headers={
        "Content-Type": "application/json", "Accept": "application/json"})
    start = time.perf_counter()
    with single.OPENER.open(req, timeout=180) as response:
        if (response.status != 200 or
                not response.headers.get("Content-Type", "").lower().startswith("application/json")):
            raise ValueError("Local model returned a non-JSON response")
        data = response.read(65537)
        if len(data) > 65536:
            raise ValueError("Local model response exceeds limit")
    obj = json.loads(data.decode("utf-8"))
    if not isinstance(obj, dict) or not isinstance(obj.get("choices"), list) or len(obj["choices"]) != 1:
        raise ValueError("Invalid local model response")
    choice = obj["choices"][0]
    if not isinstance(choice, dict) or choice.get("finish_reason") != "stop":
        raise ValueError("Incomplete or nonfinal model response")
    msg = choice.get("message")
    answer = msg.get("content") if isinstance(msg, dict) else None
    if (not isinstance(msg, dict) or msg.get("role") != "assistant" or
            (isinstance(msg.get("reasoning_content"), str) and
             msg["reasoning_content"].strip())):
        raise ValueError("Unexpected role or leaked reasoning in model response")
    if (not isinstance(answer, str) or not answer.strip() or
            len(answer) > MAX_ANSWER or THOUGHT_MARKER.search(answer) or
            "\x00" in answer):
        raise ValueError("Empty, oversized or unfiltered model answer")
    return answer, round((time.perf_counter() - start) * 1000, 1)


def validate_capture(path):
    p = Path(path)
    if not p.is_file() or p.is_symlink() or not 0 < p.stat().st_size <= MAX_CAPTURE:
        raise ValueError("Capture is missing or too large")
    doc = json.loads(p.read_text(encoding="utf-8"))
    if (not isinstance(doc, dict) or set(doc) != {"schema", "suite_sha256", "label",
            "configuration", "scenarios"} or doc["schema"] != SCHEMA or
            doc["suite_sha256"] != SUITE_HASH or
            not isinstance(doc["label"], str) or not 1 <= len(doc["label"]) <= 80 or
            any(ord(c) < 32 for c in doc["label"]) or
            not isinstance(doc["scenarios"], list) or
            len(doc["scenarios"]) != len(SCENARIOS)):
        raise ValueError("Invalid or incompatible dialogue capture")
    single.validate_configuration(doc["configuration"])
    for item, (name, _, prompts) in zip(doc["scenarios"], SCENARIOS):
        if (not isinstance(item, dict) or set(item) != {"id", "answers"} or
                item["id"] != name or not isinstance(item["answers"], list) or
                len(item["answers"]) != len(prompts)):
            raise ValueError("Dialogue capture has wrong scenario or turn count")
        for turn in item["answers"]:
            if (not isinstance(turn, dict) or set(turn) != {"text", "latency_ms"} or
                    not isinstance(turn["text"], str) or
                    not 0 < len(turn["text"]) <= MAX_ANSWER or
                    "\x00" in turn["text"] or
                    THOUGHT_MARKER.search(turn["text"]) or
                    isinstance(turn["latency_ms"], bool) or
                    not isinstance(turn["latency_ms"], (int, float)) or
                    not math.isfinite(turn["latency_ms"]) or
                    not 0 <= turn["latency_ms"] <= 180000):
                raise ValueError("Malformed dialogue response")
    return doc


def capture(args):
    if (not isinstance(args.label, str) or not 1 <= len(args.label) <= 80 or
            any(ord(c) < 32 for c in args.label)):
        raise ValueError("Invalid capture label")
    config = {"temperature": args.temperature, "top_p": args.top_p,
              "seed": args.seed, "max_tokens": 384,
              "hardware_label": args.hardware_label, "nonthinking_requested": True}
    single.validate_configuration(config)
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("Destination exists; no inference was started")
    results = []
    for name, system, prompts in SCENARIOS:
        transcript, answers = [], []
        for n, user in enumerate(prompts):
            transcript.append(("user", user))
            print("Dialogue", name, "turn", n + 1, "of", len(prompts), flush=True)
            answer, elapsed = request_once(system, transcript, args.temperature,
                                           args.top_p, args.seed)
            transcript.append(("assistant", answer))
            answers.append({"text": answer, "latency_ms": elapsed})
        results.append({"id": name, "answers": answers})
    single.save_new_json(args.output, {"schema": SCHEMA, "suite_sha256": SUITE_HASH,
        "label": args.label, "configuration": config, "scenarios": results})
    print("Saved private multi-turn capture; no model weights or personal chats copied.")


def compare(args):
    first = validate_capture(args.baseline)
    second = validate_capture(args.candidate)
    if first["label"] == second["label"] or first["configuration"] != second["configuration"]:
        raise ValueError("Distinct model labels and identical hardware/sampling required")
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("Score output exists; refusing overwrite")
    rng = random.SystemRandom()
    criteria = ("naturalness", "context_retention", "task_usefulness")
    rows = []
    for (name, _, prompts), a, b in zip(SCENARIOS, first["scenarios"], second["scenarios"]):
        reversed_ = bool(rng.randrange(2))
        variants = (b, a) if reversed_ else (a, b)
        print("\nScenario:", name, "\nReview the full dialogue, not just its final answer.")
        for v, case in enumerate(variants, 1):
            print("\nAnonymous version", v)
            for question, turn in zip(prompts, case["answers"]):
                print("USER:", question, "\nEMBER:", turn["text"])
        row = {"id": name}
        for criterion in criteria:
            while True:
                value = input(criterion + " — best version (1/2/=): ").strip()
                if value in ("1", "2", "="):
                    break
            row[criterion] = single.selected_winner(value, reversed_)
        rows.append(row)
    totals = {criterion: {k: sum(r[criterion] == k for r in rows)
                for k in ("baseline", "candidate", "tie")}
              for criterion in criteria}
    single.save_new_json(args.output, {"schema": SCORE_SCHEMA,
        "suite_sha256": SUITE_HASH, "baseline": first["label"],
        "candidate": second["label"], "hardware_label": first["configuration"]["hardware_label"],
        "scores": rows, "counts": totals,
        "note": "Subjective human preference; private generated text not exported."})
    print("Saved score-only report:", args.output, totals)


def self_test():
    import tempfile
    assert request_messages("sys", [("user", "hello")], 0.7, 0.9, 17)[
        "chat_template_kwargs"]["enable_thinking"] is False
    try:
        request_messages("sys", [("assistant", "bad")], 0.7, 0.9, 17)
        raise AssertionError("Assistant-last request accepted")
    except ValueError:
        pass
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        fixture = {"schema": SCHEMA, "suite_sha256": SUITE_HASH,
                   "label": "model-a", "configuration": {
                       "temperature": 0.7, "top_p": 0.9, "seed": 17,
                       "max_tokens": 384, "hardware_label": "test-4070",
                       "nonthinking_requested": True},
                   "scenarios": [{"id": name, "answers": [{"text": "Sample reply", "latency_ms": 1.0}
                                                for _ in prompts]}
                                 for name, _, prompts in SCENARIOS]}
        path = root / "capture.json"
        single.save_new_json(path, fixture)
        assert validate_capture(path)["label"] == "model-a"
        altered = json.loads(json.dumps(fixture))
        altered["scenarios"][0]["answers"].pop()
        single.save_new_json(root / "short.json", altered)
        try:
            validate_capture(root / "short.json")
            raise AssertionError("Missing dialogue turn accepted")
        except ValueError:
            pass
        altered = json.loads(json.dumps(fixture))
        altered["scenarios"][0]["answers"][0]["text"] = "< think >secret</ think >"
        single.save_new_json(root / "thought.json", altered)
        try:
            validate_capture(root / "thought.json")
            raise AssertionError("Hidden reasoning accepted")
        except ValueError:
            pass
        altered = json.loads(json.dumps(fixture))
        altered["scenarios"][0]["answers"][0]["latency_ms"] = float("nan")
        single.save_new_json(root / "nan.json", altered)
        try:
            validate_capture(root / "nan.json")
            raise AssertionError("Nonfinite timing accepted")
        except ValueError:
            pass
        try:
            single.save_new_json(path, fixture)
            raise AssertionError("Existing capture overwritten")
        except ValueError:
            pass
        assert single.selected_winner("1", True) == "candidate"
    print("Ember multi-turn blind dialogue A/B: synthetic-only self-test PASS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    collect = sub.add_parser("capture", help="Manually capture the selected localhost LLM")
    collect.add_argument("--label", required=True)
    collect.add_argument("--hardware-label", required=True)
    collect.add_argument("--output", required=True, type=Path)
    collect.add_argument("--temperature", type=float, default=0.7)
    collect.add_argument("--top-p", type=float, default=0.9)
    collect.add_argument("--seed", type=int, default=17)
    score = sub.add_parser("score", help="Compare two captures without network")
    score.add_argument("--baseline", required=True, type=Path)
    score.add_argument("--candidate", required=True, type=Path)
    score.add_argument("--output", required=True, type=Path)
    sub.add_parser("self-test", help="No LLM, internet or private inputs")
    args = parser.parse_args()
    try:
        if args.command == "capture":
            capture(args)
        elif args.command == "score":
            compare(args)
        else:
            self_test()
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
