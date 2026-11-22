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



# Fixture-only live tool-calling acceptance: no local files are read or edited.
# This is deliberately NOT a coding-quality evaluation.
PROBE_TOOLS = [
    {
        "type": "function",
        "function": {
            "name": "project_map",
            "description": "List source files in a chosen repository.",
            "parameters": {
                "type": "object", "properties": {},
                "required": [], "additionalProperties": False,
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "search_text",
            "description": "Find one literal source string.",
            "parameters": {
                "type": "object",
                "properties": {"query": {"type": "string"}},
                "required": ["query"], "additionalProperties": False,
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "read_source",
            "description": "Read numbered source lines within a project.",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string"},
                    "start_line": {"type": "integer", "minimum": 1},
                },
                "required": ["path"], "additionalProperties": False,
            },
        },
    },
    {
        "type": "function",
        "function": {
            "name": "propose_patch",
            "description": "Stage one exact text replacement without writing.",
            "parameters": {
                "type": "object",
                "properties": {
                    "path": {"type": "string"},
                    "old_text": {"type": "string"},
                    "new_text": {"type": "string"},
                },
                "required": ["path", "old_text", "new_text"],
                "additionalProperties": False,
            },
        },
    },
]
PROBE_STEPS = (
    ("project_map", {}, "src/example.cpp"),
    ("search_text", {"query": "answer"},
     "src/example.cpp:1: int answer(){ return 41; }"),
    ("read_source", {"path": "src/example.cpp", "start_line": 1},
     "src/example.cpp\n1: int answer(){ return 41; }"),
    ("propose_patch", {
        "path": "src/example.cpp",
        "old_text": "return 41;",
        "new_text": "return 42;",
    }, "One exact proposal staged for review, not applied. Tests not run."),
)


def inspect_function_call(response, expected_name, expected_args):
    """Validate the llama-server response shape as Ember's C++ parser does."""
    if not isinstance(response, dict):
        raise ValueError("Model output is not a JSON object")
    choices = response.get("choices")
    if not isinstance(choices, list) or len(choices) != 1:
        raise ValueError("Expected exactly one choice")
    choice = choices[0]
    if not isinstance(choice, dict) or choice.get("finish_reason") == "length":
        raise ValueError("Incomplete or invalid model answer")
    message = choice.get("message")
    if not isinstance(message, dict) or message.get("role") != "assistant":
        raise ValueError("Expected assistant message")
    calls = message.get("tool_calls")
    if not isinstance(calls, list) or len(calls) != 1:
        raise ValueError("Expected one structured tool call, got final text or parallel calls")
    call = calls[0]
    if not isinstance(call, dict) or call.get("type") != "function":
        raise ValueError("Invalid function call type")
    call_id = call.get("id")
    if not isinstance(call_id, str) or not re.fullmatch(r"[A-Za-z0-9_-]{1,100}", call_id):
        raise ValueError("Invalid tool call identifier")
    func = call.get("function")
    if not isinstance(func, dict) or func.get("name") != expected_name:
        raise ValueError("Model selected a different tool than the required probe step")
    raw = func.get("arguments")
    if not isinstance(raw, str) or not 0 < len(raw) <= 4096:
        raise ValueError("Malformed or oversized tool arguments")
    args = json.loads(raw)
    if not isinstance(args, dict) or args != expected_args:
        raise ValueError("Model did not produce exact structured fixture arguments")
    return {"id": call_id, "type": "function",
            "function": {"name": expected_name, "arguments": raw}}


def post_probe(endpoint, messages, allow_tools=True):
    validate_local(endpoint)
    payload = {
        "model": "ember",
        "messages": messages,
        "temperature": 0.0,
        "max_tokens": 2000 if allow_tools else 256,
        "stream": False,
        "parallel_tool_calls": False,
        "tool_choice": "auto" if allow_tools else "none",
    }
    if allow_tools:
        payload["tools"] = PROBE_TOOLS
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = Request(endpoint, data=data, method="POST",
                      headers={"Content-Type": "application/json",
                               "Accept": "application/json"})
    started = time.perf_counter()
    with LOCAL_OPENER.open(request, timeout=120) as response:
        if response.status != 200 or not response.headers.get(
                "Content-Type", "").lower().startswith("application/json"):
            raise ValueError("Local model did not return JSON 200")
        raw = response.read(65537)
        if not raw or len(raw) > 65536:
            raise ValueError("Local model JSON output was empty or oversized")
    elapsed = round((time.perf_counter() - started) * 1000, 2)
    doc = json.loads(raw.decode("utf-8", errors="strict"))
    return doc, elapsed


def verify_tool_sequence(endpoint=ENDPOINT):
    """Live GGUF/Jinja handshake test; NO tool effects, filesystem or Internet."""
    validate_local(endpoint)
    history = [
        {"role": "system", "content": (
            "You are checking exact tool-call serialization, not solving a "
            "real user task. Never access real files or the Internet. "
            "Follow the current user instruction and emit exactly one "
            "function call per turn, with the requested argument values. "
            "Never output code patches as prose."
        )},
        {"role": "user", "content": (
            "Perform this SYNTHETIC fixture-only sequence, one tool each turn: "
            "first project_map(), then search_text(query='answer'), then "
            "read_source(path='src/example.cpp',start_line=1), then "
            "propose_patch(path='src/example.cpp',old_text='return 41;',"
            "new_text='return 42;'). All tool results are simulated; "
            "do not execute anything."
        )},
    ]
    steps = []
    for name, arguments, result in PROBE_STEPS:
        obj, elapsed = post_probe(endpoint, history)
        call = inspect_function_call(obj, name, arguments)
        history.append({"role": "assistant", "content": None,
                        "tool_calls": [call]})
        history.append({"role": "tool", "tool_call_id": call["id"],
                        "content": result})
        steps.append({"tool": name, "protocol_valid": True,
                      "latency_ms": elapsed})
    obj, final_ms = post_probe(endpoint, history, allow_tools=False)
    choices = obj.get("choices") if isinstance(obj, dict) else None
    if not isinstance(choices, list) or len(choices) != 1 or not isinstance(
            choices[0], dict):
        raise ValueError("Missing final assistant response")
    msg = choices[0].get("message")
    if (not isinstance(msg, dict) or
            msg.get("role") != "assistant" or msg.get("tool_calls") or
            not isinstance(msg.get("content"), str) or
            not msg["content"].strip()):
        raise ValueError("Local model did not produce a final text answer")
    # Do not save or log the model's response, prompt, weights or local data.
    return {
        "fixture_only": True,
        "model_calls": 5,
        "valid_tool_calls": len(steps),
        "final_text_nonempty": True,
        "total_latency_ms": round(sum(step["latency_ms"] for step in steps)
                                  + final_ms, 2),
        "steps": steps,
        "note": "All simulated, no filesystem edits, shell, Git, Drive, "
                "downloads, external hosts or schedules. A pass verifies "
                "function-calling format only, not coding competence.",
    }


def tool_probe_self_test():
    expected = PROBE_STEPS[-1]
    call = {
        "id": "call_fixture_1", "type": "function",
        "function": {
            "name": expected[0],
            "arguments": json.dumps(expected[1], ensure_ascii=False),
        },
    }
    valid = {
        "choices": [{"finish_reason": "tool_calls",
                     "message": {"role": "assistant", "content": None,
                                 "tool_calls": [call]}}],
    }
    assert inspect_function_call(valid, expected[0], expected[1])["id"] == "call_fixture_1"
    for malformed in [
        {"choices": []},
        {"choices": [{"finish_reason": "length", "message": valid["choices"][0]["message"]}]},
        {"choices": [{"finish_reason": "stop",
                      "message": {"role": "assistant", "content": "I edited it."}}]},
        {"choices": [{"finish_reason": "tool_calls",
                      "message": {"role": "assistant",
                                  "tool_calls": [call, call]}}]},
    ]:
        try:
            inspect_function_call(malformed, expected[0], expected[1])
            raise AssertionError("Rejected malformed model tool call was accepted")
        except ValueError:
            pass
    for remote in ["https://example.com/v1/chat/completions",
                   "http://127.0.0.1:8080/v1/chat/completions?leak=1",
                   "http://localhost:43821/v1/chat/completions"]:
        try:
            validate_local(remote)
            raise AssertionError("Unapproved network destination accepted")
        except ValueError:
            pass

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
    tool_probe_self_test()
    print("text_benchmark local-only SSE and tool-call parser self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--tool-verify", action="store_true",
                        help="Explicit on-demand 4-tool + final-answer test "
                             "against an already running local GGUF; no tools executed")
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--output", type=Path,
                        help="Optional JSON metrics file; no model output text")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.tool_verify:
        try:
            report = verify_tool_sequence()
        except (OSError, HTTPError, URLError, ValueError,
                json.JSONDecodeError) as exc:
            parser.error("Local tool-call handshake failed: " +
                         type(exc).__name__ + ": " + str(exc))
        content = json.dumps(report, indent=2, ensure_ascii=False)
        if args.output:
            args.output.write_text(content + "\n", encoding="utf-8")
        print(content)
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
