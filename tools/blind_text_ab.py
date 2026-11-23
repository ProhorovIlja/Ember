#!/usr/bin/env python3
"""Manual blind A/B text-quality lab for locally installed Ember LLMs.

Run explicit capture separately for each installed GGUF. Responses are stored
only in the user-chosen local JSON file; scoring exports numeric results only.
No scheduled work, cloud calls, private prompts, or automatic model downloads.
"""
import argparse
import hashlib
import json
import random
import time
from pathlib import Path
from urllib.request import ProxyHandler, HTTPRedirectHandler, Request, build_opener
from urllib.parse import urlsplit

ENDPOINT = "http://127.0.0.1:43821/v1/chat/completions"
MAX_REPORT_BYTES = 256 * 1024
MAX_ANSWER_CHARS = 12000
CASES = (
    ("conversation-en", "You are having a normal conversation in English. Keep it human and concise.",
     "I had a frustrating morning. I finally finished a hard task, but I still feel oddly flat. What do you think?"),
    ("conversation-ru", "Ответь по-русски живо, естественно, без официоза и ненужных заверений.",
     "Я наконец-то исправил ошибку, над которой сидел весь вечер. Но теперь не хочется ничего делать. Как бы ты продолжил разговор?"),
    ("story-en", "Continue a collaborative Gothic story with believable characters, not purple prose.",
     "In a rain-soaked bell tower, Mara finds a letter addressed in her own handwriting. Write the next scene with dialogue."),
    ("story-continuity", "Maintain character consistency and preserve the two facts provided.",
     "Earlier: Mara is afraid of deep water. Her friend Leon cannot read. In 120 words or fewer, show them finding a sealed message beside a flooded stairwell."),
    ("troubleshooting", "Give cautious, prioritized troubleshooting without inventing machine access.",
     "A Windows desktop app freezes after changing audio devices. Logs show the old audio sink is still referenced. What would you inspect first?"),
    ("development", "Give safe, concrete coding advice. You have no tools or repository access in this exercise.",
     "A C++ callback captures a local variable by reference, then runs after the owner is destroyed. Identify the bug and a reliable fix."),
    ("instruction", "Follow the instruction exactly. No preamble.",
     "Reply with exactly: VALIDATED"),
)
PROMPT_HASH = hashlib.sha256(json.dumps(CASES, ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()
OPENER = build_opener(ProxyHandler({}), type("NoRedirect", (HTTPRedirectHandler,),
                         {"redirect_request": lambda self, *args: None})())


def validate_endpoint(url):
    p = urlsplit(url)
    if (p.scheme != "http" or p.hostname != "127.0.0.1" or
            p.path != "/v1/chat/completions" or not p.port or
            p.username or p.password or p.query or p.fragment):
        raise ValueError("Only the fixed localhost Ember text endpoint is allowed")


def one_response(system, prompt, temperature, top_p, seed):
    validate_endpoint(ENDPOINT)
    body = json.dumps({
        "model": "ember", "stream": False,
        "messages": [{"role": "system", "content": system},
                     {"role": "user", "content": prompt}],
        "temperature": temperature, "top_p": top_p,
        "seed": seed, "max_tokens": 384
    }, ensure_ascii=False).encode("utf-8")
    req = Request(ENDPOINT, data=body, method="POST",
                  headers={"Content-Type": "application/json", "Accept": "application/json"})
    start = time.perf_counter()
    with OPENER.open(req, timeout=180) as response:
        if response.status != 200 or not response.headers.get("Content-Type", "").lower().startswith("application/json"):
            raise ValueError("LLM returned a non-JSON success response")
        data = response.read(65537)
        if len(data) > 65536:
            raise ValueError("LLM response too large")
    obj = json.loads(data.decode("utf-8"))
    if not isinstance(obj, dict) or not isinstance(obj.get("choices"), list) or len(obj["choices"]) != 1:
        raise ValueError("Invalid local model response")
    choice = obj["choices"][0]
    if not isinstance(choice, dict) or choice.get("finish_reason") == "length":
        raise ValueError("Incomplete text answer")
    message = choice.get("message")
    answer = message.get("content") if isinstance(message, dict) else None
    if not isinstance(answer, str) or not answer.strip() or len(answer) > MAX_ANSWER_CHARS:
        raise ValueError("Missing, oversized or non-text model answer")
    return answer, round((time.perf_counter() - start) * 1000, 1)


def read_capture(path):
    file = Path(path)
    if not file.is_file() or file.stat().st_size > MAX_REPORT_BYTES:
        raise ValueError("Capture is missing or exceeds local size limit")
    report = json.loads(file.read_text(encoding="utf-8"))
    if (not isinstance(report, dict) or report.get("schema") != "ember-text-blind-ab-v1"
            or report.get("prompt_sha256") != PROMPT_HASH
            or not isinstance(report.get("label"), str)
            or not 1 <= len(report["label"]) <= 80
            or not isinstance(report.get("configuration"), dict)
            or not isinstance(report.get("cases"), list)
            or len(report["cases"]) != len(CASES)):
        raise ValueError("Invalid or incompatible capture")
    for actual, expected in zip(report["cases"], CASES):
        if (not isinstance(actual, dict) or actual.get("id") != expected[0]
                or not isinstance(actual.get("answer"), str)
                or not 0 < len(actual["answer"]) <= MAX_ANSWER_CHARS
                or not isinstance(actual.get("latency_ms"), (int, float))
                or isinstance(actual.get("latency_ms"), bool)
                or not 0 <= actual["latency_ms"] <= 180000):
            raise ValueError("Invalid capture case")
    return report


def check_pair(first, second):
    if first["label"] == second["label"]:
        raise ValueError("Models must have distinct labels")
    if first["prompt_sha256"] != second["prompt_sha256"] or first["configuration"] != second["configuration"]:
        raise ValueError("Both captures must use identical prompts, sampling and hardware label")


def save_new_json(path, payload):
    file = Path(path)
    if file.exists():
        raise ValueError("Output already exists: refusing to overwrite")
    file.write_text(json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def capture(args):
    if not args.label or len(args.label) > 80 or any(ord(c) < 32 for c in args.label):
        raise ValueError("Label must be a short printable name")
    if not 0.1 <= args.temperature <= 1.5 or not 0.1 <= args.top_p <= 1:
        raise ValueError("Sampling parameters out of range")
    if not 0 <= args.seed < 2147483600:
        raise ValueError("Seed out of range")
    if not args.hardware_label or len(args.hardware_label) > 80:
        raise ValueError("Record the same human-entered hardware label for paired tests")
    if args.output.exists():
        raise ValueError("Destination exists; not running inference or overwriting capture")
    config = {"temperature": args.temperature, "top_p": args.top_p,
              "seed": args.seed, "max_tokens": 384,
              "hardware_label": args.hardware_label}
    cases = []
    for i, (id_, system, prompt) in enumerate(CASES):
        print("Evaluating case", i + 1, "of", len(CASES), id_, flush=True)
        answer, ms = one_response(system, prompt, args.temperature, args.top_p, args.seed)
        cases.append({"id": id_, "answer": answer, "latency_ms": ms})
    save_new_json(args.output, {"schema": "ember-text-blind-ab-v1", "label": args.label,
                                "prompt_sha256": PROMPT_HASH,
                                "configuration": config, "cases": cases})
    print("Saved local-only capture. It contains complete generated answers; keep it private.")


def score(args):
    first, second = read_capture(args.baseline), read_capture(args.candidate)
    check_pair(first, second)
    if args.output.exists():
        raise ValueError("Output exists; scores were not changed")
    print("No model labels will be shown until after scoring. Rate naturalness and task usefulness.")
    rng = random.SystemRandom()
    rows = []
    for (id_, system, prompt), a, b in zip(CASES, first["cases"], second["cases"]):
        reversed_ = bool(rng.randrange(2))
        left, right = (b, a) if reversed_ else (a, b)
        print("\nCASE:", id_, "\nPROMPT:", prompt,
              "\n\n[1]\n", left["answer"], "\n\n[2]\n", right["answer"])
        row = {"id": id_}
        for criterion in ("naturalness", "usefulness"):
            while True:
                choice = input(criterion + " (1/2/= for tie): ").strip()
                if choice in ("1", "2", "="):
                    break
            row[criterion] = "tie" if choice == "=" else (
                "candidate" if (choice == "1") == reversed_ else "baseline")
        rows.append(row)
    counts = {criterion: {k: sum(r[criterion] == k for r in rows)
                         for k in ("baseline", "candidate", "tie")}
              for criterion in ("naturalness", "usefulness")}
    save_new_json(args.output, {
        "schema": "ember-text-ab-scores-v1",
        "prompt_sha256": PROMPT_HASH,
        "hardware_label": first["configuration"]["hardware_label"],
        "baseline": first["label"], "candidate": second["label"],
        "scores": rows, "counts": counts,
        "note": "Human preference, not model intelligence proof; no generated text is saved here."
    })
    print("Saved score-only report:", args.output, counts)


def self_test():
    validate_endpoint(ENDPOINT)
    for endpoint in ("https://example.com/v1/chat/completions",
                     "http://127.0.0.1:1/v1/chat/completions?private=1",
                     "http://localhost:43821/v1/chat/completions"):
        try:
            validate_endpoint(endpoint)
        except ValueError:
            pass
        else:
            raise AssertionError("Unsafe endpoint accepted")
    import tempfile
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "first.json"
        base = {"schema": "ember-text-blind-ab-v1", "label": "model-a",
                "prompt_sha256": PROMPT_HASH,
                "configuration": {"temperature": 0.7, "top_p": 0.9,
                                  "seed": 17, "max_tokens": 384,
                                  "hardware_label": "test-4070"},
                "cases": [{"id": id_, "answer": "A safe synthetic test answer",
                           "latency_ms": 20} for id_, _, _ in CASES]}
        save_new_json(path, base)
        assert read_capture(path)["label"] == "model-a"
        other = dict(base, label="model-b")
        check_pair(base, other)
        try:
            check_pair(base, dict(other, configuration={"temperature": 0.3}))
            raise AssertionError("Mismatched model sampling accepted")
        except ValueError:
            pass
        other["cases"] = [dict(x) for x in other["cases"]]
        other["cases"][0]["id"] = "wrong"
        save_new_json(Path(directory) / "bad.json", other)
        try:
            read_capture(Path(directory) / "bad.json")
            raise AssertionError("Mismatched prompt order accepted")
        except ValueError:
            pass
        try:
            save_new_json(path, base)
            raise AssertionError("Existing capture overwritten")
        except ValueError:
            pass
    print("blind text A/B protocol self-test passed (no inference, no network)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    collect = sub.add_parser("capture", help="Manually test the currently running localhost model")
    collect.add_argument("--label", required=True)
    collect.add_argument("--output", required=True, type=Path)
    collect.add_argument("--hardware-label", required=True)
    collect.add_argument("--temperature", type=float, default=0.7)
    collect.add_argument("--top-p", type=float, default=0.9)
    collect.add_argument("--seed", type=int, default=17)
    review = sub.add_parser("score", help="Offline blind human ranking, no API calls")
    review.add_argument("--baseline", required=True, type=Path)
    review.add_argument("--candidate", required=True, type=Path)
    review.add_argument("--output", required=True, type=Path)
    sub.add_parser("self-test", help="No model or network needed")
    args = parser.parse_args()
    try:
        if args.command == "capture": capture(args)
        elif args.command == "score": score(args)
        else: self_test()
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.error(str(exc))


if __name__ == "__main__":
    main()
