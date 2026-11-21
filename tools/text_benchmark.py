#!/usr/bin/env python3
"""Local, metrics-only llama.cpp SSE text benchmark; never saves user prompts or responses."""
import argparse
import json
import math
import re
import statistics
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

ENDPOINT = "http://127.0.0.1:43821/v1/chat/completions"
MAX_EVENTS = 4096
MAX_CHARS = 32768
SCENARIOS = (
    ("instruction", "Reply with exactly the word READY."),
    ("story", "In English, write a short Gothic scene in a bell tower with vivid character dialogue."),
    ("russian", "Одним предложением по-русски объясни, что делает графический процессор."),
    ("development", "In three sentences, explain how to safely check a deadlocked worker thread."),
)


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


LOCAL_OPENER = build_opener(ProxyHandler({}), NoRedirect())


def validate_local(endpoint):
    url = urlsplit(endpoint)
    if (url.scheme != "http" or url.hostname != "127.0.0.1" or
            url.port is None or url.username or url.password or
            url.path != "/v1/chat/completions" or url.query or url.fragment):
        raise ValueError("Only explicit localhost llama.cpp chat endpoints are allowed")


def measure(endpoint, scenario, prompt):
    validate_local(endpoint)
    if scenario not in {name for name, _ in SCENARIOS} or not 0 < len(prompt) <= 4096:
        raise ValueError("Invalid scenario or prompt")
    payload = json.dumps({
        "model": "ember",
        "messages": [
            {"role": "system", "content": "Follow the current user request. Do not invent tool use."},
            {"role": "user", "content": prompt},
        ],
        "temperature": 0.2, "max_tokens": 256, "stream": True,
        "stream_options": {"include_usage": True},
    }, ensure_ascii=False).encode("utf-8")
    request = Request(endpoint, data=payload, method="POST",
                      headers={"Content-Type": "application/json",
                               "Accept": "text/event-stream"})
    first_ms = None
    chunks = 0
    chars = 0
    completion_tokens = None
    done = False
    instruction_ok = None
    utf8_data = []
    started = time.perf_counter()
    with LOCAL_OPENER.open(request, timeout=120) as response:
        if response.status != 200 or not response.headers.get(
                "Content-Type", "").lower().startswith("text/event-stream"):
            raise ValueError("Local LLM did not return streaming SSE")
        events = 0
        for raw_line in response:
            if len(raw_line) > 131072:
                raise ValueError("Excessive SSE line")
            line = raw_line.strip()
            if not line.startswith(b"data:"):
                continue
            events += 1
            if events > MAX_EVENTS:
                raise ValueError("Excessive SSE event count")
            payload_line = line[5:].strip()
            if payload_line == b"[DONE]":
                done = True
                break
            obj = json.loads(payload_line.decode("utf-8", errors="strict"))
            if not isinstance(obj, dict) or "error" in obj:
                raise ValueError("Local LLM emitted an error or malformed SSE object")
            usage = obj.get("usage")
            if isinstance(usage, dict) and isinstance(
                    usage.get("completion_tokens"), int):
                completion_tokens = usage["completion_tokens"]
            for choice in obj.get("choices", []):
                if not isinstance(choice, dict):
                    raise ValueError("Malformed SSE choice")
                delta = choice.get("delta") or {}
                value = delta.get("content") if isinstance(delta, dict) else None
                if value is None:
                    continue
                if not isinstance(value, str):
                    raise ValueError("Non-text SSE content")
                if value:
                    if first_ms is None:
                        first_ms = (time.perf_counter() - started) * 1000
                    chunks += 1
                    chars += len(value)
                    if chars > MAX_CHARS:
                        raise ValueError("Excessive local generated text")
                    if scenario == "instruction":
                        utf8_data.append(value)
    total_ms = (time.perf_counter() - started) * 1000
    if not done or not chunks or first_ms is None:
        raise ValueError("Incomplete or empty local SSE stream")
    if scenario == "instruction":
        instruction_ok = "".join(utf8_data).strip() == "READY"
    active_seconds = max(0.001, (total_ms - first_ms) / 1000)
    return {
        "scenario": scenario, "ttft_ms": round(first_ms, 2),
        "full_response_ms": round(total_ms, 2),
        "streamed_chunks": chunks, "output_characters": chars,
        "output_characters_per_second": round(chars / active_seconds, 2),
        "completion_tokens_reported": completion_tokens,
        "completion_tokens_per_second": (
            round(completion_tokens / active_seconds, 2)
            if completion_tokens is not None else None),
        "instruction_exact_match": instruction_ok,
    }


def p95(values):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)]


def self_test():
    class Mock(BaseHTTPRequestHandler):
        content_type = "text/event-stream"
        send_done = True

        def do_POST(self):
            assert self.path == "/v1/chat/completions"
            length = int(self.headers["Content-Length"])
            assert 0 < length < 8192
            body = json.loads(self.rfile.read(length))
            assert body["model"] == "ember" and body["stream"] is True
            assert body["stream_options"]["include_usage"] is True
            self.send_response(200)
            self.send_header("Content-Type", self.content_type)
            self.end_headers()
            for event in [
                {"choices": [{"delta": {"content": "READY"}}]},
                {"choices": [], "usage": {"completion_tokens": 1}},
            ]:
                self.wfile.write(("data: " + json.dumps(event) + "\n\n").encode("utf-8"))
                self.wfile.flush()
            if self.send_done:
                self.wfile.write(b"data: [DONE]\n\n")
                self.wfile.flush()

        def log_message(self, *_args):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Mock)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    endpoint = "http://127.0.0.1:" + str(server.server_port) + "/v1/chat/completions"
    try:
        result = measure(endpoint, "instruction", "Reply with exactly READY.")
        assert result["instruction_exact_match"] is True
        assert result["completion_tokens_reported"] == 1
        assert result["output_characters"] == 5
        assert result["ttft_ms"] >= 0
        Mock.send_done = False
        try:
            measure(endpoint, "instruction", "Reply with READY.")
            raise AssertionError("Incomplete SSE accepted")
        except ValueError:
            pass
        Mock.send_done = True
        Mock.content_type = "text/html"
        try:
            measure(endpoint, "instruction", "Reply with READY.")
            raise AssertionError("Unexpected content-type accepted")
        except ValueError:
            pass
        for remote in ["https://example.com/v1/chat/completions",
                       "http://127.0.0.1:9999/v1/audio/speech",
                       "http://127.0.0.1:9999/v1/chat/completions?leak=1"]:
            try:
                validate_local(remote)
                raise AssertionError("Non-loopback chat URL accepted")
            except ValueError:
                pass
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=3)
    print("text_benchmark local-only SSE metrics self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--output", type=Path,
                        help="Optional JSON metrics file; no model output text")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not 1 <= args.samples <= 40:
        parser.error("--samples must be between 1 and 40")
    records = []
    for index in range(args.samples):
        scenario, prompt = SCENARIOS[index % len(SCENARIOS)]
        try:
            records.append(measure(ENDPOINT, scenario, prompt))
        except (OSError, HTTPError, URLError, ValueError, json.JSONDecodeError) as exc:
            parser.error("Local trial " + str(index + 1) +
                         " failed: " + type(exc).__name__ + ": " + str(exc))
    report = {
        "note": "Metrics-only local llama.cpp streaming test. No responses or prompts saved. Characters/s is NOT tokens/s; token rate requires server usage. Not a reasoning-quality benchmark.",
        "samples": len(records),
        "p50_ttft_ms": round(statistics.median(r["ttft_ms"] for r in records), 2),
        "p95_ttft_ms": p95([r["ttft_ms"] for r in records]),
        "p50_full_response_ms": round(statistics.median(
            r["full_response_ms"] for r in records), 2),
        "instruction_exact_match_all": all(
            r["instruction_exact_match"] for r in records
            if r["scenario"] == "instruction"),
        "trials": records,
    }
    content = json.dumps(report, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(content + "\n", encoding="utf-8")
    print(content)


if __name__ == "__main__":
    main()
