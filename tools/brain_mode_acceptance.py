#!/usr/bin/env python3
"""One-shot, metrics-only Qwen3.5/llama.cpp reasoning-mode contract test.

Run manually against an ALREADY STARTED managed Windows llama-server.
No model downloads, inference-server launches, file access beyond an
optional NEW report, recurring work, personal prompt capture, response
logging, or cloud requests. Foreground Windows netstat checks the listener PID.
Synthetic, built-in prompts only. No external network host is configurable.
This is a protocol acceptance test, NOT an evaluation of naturalness or voice.
"""
import argparse
import io
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import time
from urllib.error import HTTPError, URLError
from urllib.request import ProxyHandler, HTTPRedirectHandler, Request, build_opener

ENDPOINT = "http://127.0.0.1:43821/v1/chat/completions"
PORT = 43821
MAX_LINE = 131072
MAX_EVENTS = 8192
MAX_CONTENT = 32768
MAX_TOTAL_BYTES = 512 * 1024
THOUGHT = re.compile(r"<\s*/?\s*think\b", re.IGNORECASE)

# No user-authored conversations, source files, image data or account tokens.
CASES = (
    ("voice_nonthinking", False, False,
     "Answer in natural English, exactly one word: EMBER_READY.",
     "EMBER_READY"),
    ("russian_nonthinking", False, False,
     "Ответь только одним словом, заглавными буквами: ГОТОВО.",
     "ГОТОВО"),
    ("dialogue_deep", True, False,
     "In one sentence, explain why a good dialogue partner remembers a correction.",
     None),
    ("coding_deep", True, True,
     "Briefly explain why capturing a short-lived C++ local variable by reference in a deferred callback is unsafe.",
     None),
)

class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None

CLIENT = build_opener(ProxyHandler({}), NoRedirect())


def owner_pids(netstat_output):
    """Strict 127.0.0.1:43821 listening owner, not a wildcard listener."""
    owners = set()
    for line in netstat_output.splitlines():
        fields = line.split()
        if (len(fields) >= 5 and fields[0].upper() == "TCP" and
                fields[1] == f"127.0.0.1:{PORT}" and
                fields[2] == "0.0.0.0:0" and fields[-1].isdigit()):
            owners.add(int(fields[-1]))
    return owners


def require_owner(expected_pid):
    if os.name != "nt":
        raise ValueError("Live acceptance requires Windows netstat PID ownership")
    if type(expected_pid) is not int or expected_pid <= 0:
        raise ValueError("Supply exact user-started llama-server --pid")
    result = subprocess.run(["netstat", "-ano", "-p", "tcp"],
                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, check=False, timeout=10)
    if result.returncode or owner_pids(result.stdout.decode("ascii", "ignore")) != {expected_pid}:
        raise ValueError("Managed 127.0.0.1:43821 owner PID changed or is unverified")


def model_payload(case):
    name, thinking, precise_code, question, _exact = case
    if name not in {item[0] for item in CASES}:
        raise ValueError("Unknown synthetic scenario")
    if not isinstance(question, str) or not question or len(question) > 1024:
        raise ValueError("Invalid synthetic prompt")
    coding = thinking and precise_code
    return {
        "model": "ember",
        "messages": [
            {"role": "system", "content": "You are Ember. Follow the synthetic test precisely. Never use tools."},
            {"role": "user", "content": question},
        ],
        "max_tokens": 1536 if thinking else 192,
        "temperature": 0.6 if coding else (1.0 if thinking else 0.7),
        "top_p": 0.95 if thinking else 0.8,
        "top_k": 20,
        "min_p": 0.0,
        "repeat_penalty": 1.0,
        "presence_penalty": 0.0 if coding else 1.5,
        "chat_template_kwargs": {"enable_thinking": thinking},
        "stream": True,
        "stream_options": {"include_usage": True},
    }


def parse_sse(stream, started, expected_exact=None, allow_reasoning=False):
    """Bounded line reader, no generated text is retained in any report."""
    seen_done = False
    final_reason = None
    first_visible_ms = None
    visible_chars = 0
    reasoning_chars = 0
    total_bytes = 0
    events = 0
    # Keep at most the small exact-answer value; no full generated response.
    exact_answer = "" if expected_exact is not None else None
    # Track tag starts even when split across SSE frames, with <= 24 chars.
    marker_tail = ""
    completion_tokens = None
    while True:
        raw = stream.readline(MAX_LINE + 1)
        if not raw:
            break
        total_bytes += len(raw)
        if len(raw) > MAX_LINE or total_bytes > MAX_TOTAL_BYTES:
            raise ValueError("Oversized streaming response")
        line = raw.strip()
        if not line or line.startswith(b":"):
            continue
        if not line.startswith(b"data:"):
            raise ValueError("Unexpected streaming protocol line")
        events += 1
        if events > MAX_EVENTS:
            raise ValueError("Too many model events")
        data = line[5:].strip()
        if data == b"[DONE]":
            seen_done = True
            break
        try:
            event = json.loads(data.decode("utf-8", "strict"))
        except (ValueError, UnicodeError) as exc:
            raise ValueError("Invalid SSE JSON") from exc
        if not isinstance(event, dict) or "error" in event:
            raise ValueError("Local server returned error event")
        usage = event.get("usage")
        if isinstance(usage, dict):
            tokens = usage.get("completion_tokens")
            if type(tokens) is int and 0 <= tokens <= 100000:
                completion_tokens = tokens
        choices = event.get("choices")
        if not isinstance(choices, list) or len(choices) > 1:
            raise ValueError("Unexpected SSE choices")
        for choice in choices:
            if not isinstance(choice, dict):
                raise ValueError("Malformed completion choice")
            reason = choice.get("finish_reason")
            if reason is not None:
                if not isinstance(reason, str) or final_reason is not None:
                    raise ValueError("Invalid or duplicate finish reason")
                final_reason = reason
            delta = choice.get("delta")
            if delta is None:
                continue
            if not isinstance(delta, dict):
                raise ValueError("Invalid SSE delta")
            hidden = delta.get("reasoning_content")
            if hidden is not None:
                if not isinstance(hidden, str):
                    raise ValueError("Invalid reasoning fragment")
                reasoning_chars += len(hidden)
                if not allow_reasoning and hidden.strip():
                    raise ValueError("Nonthinking leaked reasoning_content")
            content = delta.get("content")
            if content is not None:
                if not isinstance(content, str):
                    raise ValueError("Non-text content in language-model stream")
                if content:
                    if first_visible_ms is None:
                        first_visible_ms = round((time.perf_counter() - started) * 1000, 2)
                    visible_chars += len(content)
                    if visible_chars > MAX_CONTENT:
                        raise ValueError("Excessive visible model text")
                    combined = marker_tail + content
                    if THOUGHT.search(combined):
                        raise ValueError("Model thinking markers appeared in visible text")
                    marker_tail = combined[-24:]
                    if exact_answer is not None:
                        if visible_chars > len(expected_exact) + 2:
                            raise ValueError("Synthetic exact-answer response has surplus text")
                        exact_answer += content
    elapsed = round((time.perf_counter() - started) * 1000, 2)
    if not seen_done or final_reason != "stop" or visible_chars == 0:
        raise ValueError("Missing visible completed answer / [DONE] / stop")
    if expected_exact is not None and exact_answer.strip() != expected_exact:
        raise ValueError("Synthetic instruction exact-match failed")
    return {
        "passed": True,
        "first_visible_ms": first_visible_ms,
        "total_ms": elapsed,
        "visible_characters": visible_chars,
        "reasoning_characters": reasoning_chars,
        "server_completion_tokens": completion_tokens,
        "finish_reason": final_reason,
    }


def run_case(case, expected_pid):
    require_owner(expected_pid)
    body = json.dumps(model_payload(case), ensure_ascii=False).encode("utf-8")
    if len(body) > 8192:
        raise ValueError("Unexpectedly large synthetic payload")
    request = Request(ENDPOINT, data=body, method="POST", headers={
        "Content-Type": "application/json", "Accept": "text/event-stream"})
    started = time.perf_counter()
    with CLIENT.open(request, timeout=150) as reply:
        if (reply.status != 200 or not reply.headers.get(
                "Content-Type", "").lower().startswith("text/event-stream")):
            raise ValueError("Managed llama-server did not return SSE HTTP 200")
        result = parse_sse(reply, started, expected_exact=case[4], allow_reasoning=case[1])
    require_owner(expected_pid)  # Fail if the listener was replaced mid-run.
    return {"scenario": case[0], "thinking_requested": case[1], **result}


def write_new_report(path, report):
    if path is None:
        return
    if path.is_symlink() or not path.parent.is_dir() or path.parent.is_symlink():
        raise ValueError("Use a new local report path in an existing real directory")
    content = (json.dumps(report, indent=2, sort_keys=True) + "\n").encode("utf-8")
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as file:
        file.write(content)
        file.flush()
        os.fsync(file.fileno())


def self_test():
    assert owner_pids("TCP 127.0.0.1:43821 0.0.0.0:0 LISTENING 1234\n") == {1234}
    assert not owner_pids("TCP 0.0.0.0:43821 0.0.0.0:0 LISTENING 1234\n")
    payloads = [model_payload(case) for case in CASES]
    assert [p["temperature"] for p in payloads] == [0.7, 0.7, 1.0, 0.6]
    assert [p["presence_penalty"] for p in payloads] == [1.5, 1.5, 1.5, 0.0]
    assert [p["chat_template_kwargs"]["enable_thinking"] for p in payloads] == [False, False, True, True]
    def event(content="", reasoning="", finish=None):
        return (b"data: " + json.dumps({"choices": [{"delta": {
            "content": content, "reasoning_content": reasoning},
            "finish_reason": finish}]}).encode() + b"\n\n")
    def read(data, **kwargs):
        return parse_sse(io.BytesIO(data), time.perf_counter(), **kwargs)
    okay = event("EMBER_") + event("READY", finish="stop") + b"data: [DONE]\n\n"
    assert read(okay, expected_exact="EMBER_READY")["visible_characters"] == 11
    deep = event("", "private") + event("Visible answer.", finish="stop") + b"data: [DONE]\n\n"
    assert read(deep, allow_reasoning=True)["reasoning_characters"] == 7
    invalid_cases = [
        (deep, {"allow_reasoning": False}),
        (event("<thi") + event("nk>secret", finish="stop") + b"data: [DONE]\n\n", {}),
        (event("anything", finish="length") + b"data: [DONE]\n\n", {}),
        (event("anything", finish="stop"), {}),
        (event("", "hidden", finish="stop") + b"data: [DONE]\n\n", {"allow_reasoning": True}),
        (event("WRONG", finish="stop") + b"data: [DONE]\n\n", {"expected_exact": "EMBER_READY"}),
        (event("EMBER_READY plus extra commentary", finish="stop") + b"data: [DONE]\n\n", {"expected_exact": "EMBER_READY"}),
    ]
    for data, opts in invalid_cases:
        try:
            read(data, **opts)
        except ValueError:
            pass
        else:
            raise AssertionError("Malformed SSE should fail closed")
    print("brain_mode_acceptance: 4 payload and 9 stream-fixture checks passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Synthetic tests; no network or process launches")
    parser.add_argument("--pid", type=int, help="PID of an already running, user-started Windows llama-server")
    parser.add_argument("--output", type=Path, help="Optional NEW metrics-only private JSON file")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if os.name != "nt" or args.pid is None or args.pid <= 0:
        parser.error("Live use requires Windows and --pid of the already running llama-server")
    if args.output and (args.output.exists() or args.output.is_symlink() or
                        not args.output.parent.is_dir() or args.output.parent.is_symlink()):
        parser.error("Output must be a new local file in an existing real directory")
    rows = []
    try:
        for case in CASES:
            row = run_case(case, args.pid)
            rows.append(row)
            print(case[0] + ": PASS; visible latency " + str(row["first_visible_ms"]) + " ms", flush=True)
        report = {
            "schema": "ember-brain-mode-acceptance-v1",
            "result": "PASS",
            "local_host_only": True,
            "synthetic_prompts_only": True,
            "reply_text_saved": False,
            "tests": rows,
            "median_first_visible_ms": round(statistics.median(
                row["first_visible_ms"] for row in rows), 2),
            "note": "Protocol and basic instruction-following only; not voice naturalness, model quality, GPU VRAM, coding correctness or official GGUF-template acceptance.",
        }
        write_new_report(args.output, report)
        print(json.dumps(report, ensure_ascii=False, indent=2))
    except (ValueError, OSError, HTTPError, URLError, TimeoutError) as exc:
        # Never print raw local model responses or HTTP error response bodies.
        safe_reason = (str(exc)[:240] if isinstance(exc, ValueError) else
                       "HTTP " + str(exc.code) if isinstance(exc, HTTPError) else
                       "Local transport failed")
        parser.error("FAIL: " + type(exc).__name__ + ": " + safe_reason)


if __name__ == "__main__":
    main()
