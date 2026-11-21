#!/usr/bin/env python3
"""Local-only voice latency probe. Does not record, save or play any audio."""
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
from urllib.request import ProxyHandler, Request, build_opener

ENDPOINT = "http://127.0.0.1:43822/v1/audio/speech"
PROMPTS = (
    "The train is leaving in fifteen minutes. Would you like me to check the next connection?",
    "I thought you had already left. Well, that is a wonderful surprise!",
    "The graphics card has twelve gigabytes of video memory. Let us measure it carefully.",
    "Wait, is that really what you meant? Tell me more.",
)
MAX_BYTES = 12 * 1024 * 1024
OPENER = build_opener(ProxyHandler({}))


def percentile95(values):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)]


def measure(endpoint, model, voice, prompt):
    if not (endpoint.startswith("http://127.0.0.1:") and
            endpoint.endswith("/v1/audio/speech")):
        raise ValueError("Only loopback HTTP TTS endpoints are allowed")
    payload = json.dumps({
        "model": model, "voice": voice, "input": prompt,
        "response_format": "pcm", "stream_format": "audio",
    }, separators=(",", ":")).encode("utf-8")
    req = Request(endpoint, payload, method="POST", headers={
        "Content-Type": "application/json", "Accept": "audio/pcm",
    })
    started = time.perf_counter()
    with OPENER.open(req, timeout=90) as response:
        content_type = response.headers.get("Content-Type", "").lower()
        rate_raw = response.headers.get("X-Sample-Rate", "")
        try:
            rate = int(rate_raw)
        except ValueError as exc:
            raise ValueError("Missing or invalid PCM sample-rate header") from exc
        if not content_type.startswith("audio/pcm") or not 8000 <= rate <= 96000:
            raise ValueError("Unexpected content type or PCM sample rate")
        first = response.read(1)
        if not first:
            raise ValueError("Empty native PCM response")
        first_ms = (time.perf_counter() - started) * 1000
        count = len(first)
        while True:
            chunk = response.read(65536)
            if not chunk:
                break
            count += len(chunk)
            if count > MAX_BYTES:
                raise ValueError("PCM exceeded the 12 MiB local safety budget")
    total_ms = (time.perf_counter() - started) * 1000
    if count % 2 or count < 2:
        raise ValueError("PCM16 response ended with incomplete sample frames")
    duration = count / (2 * rate)
    return {
        "sample_rate_hz": rate,
        "pcm_bytes": count,
        "audio_seconds": round(duration, 3),
        "first_http_pcm_byte_ms": round(first_ms, 2),
        "total_http_synthesis_ms": round(total_ms, 2),
        "audio_seconds_per_wall_second": round(duration / (total_ms / 1000), 2),
    }


def self_test():
    class Handler(BaseHTTPRequestHandler):
        sample_rate = 24000
        payload = b"\x00\x00" * 2400

        def do_POST(self):
            assert self.path == "/v1/audio/speech"
            n = int(self.headers["Content-Length"])
            body = json.loads(self.rfile.read(n))
            assert body["response_format"] == "pcm"
            assert body["stream_format"] == "audio"
            self.send_response(200)
            self.send_header("Content-Type", "audio/pcm")
            self.send_header("X-Sample-Rate", str(self.sample_rate))
            self.send_header("Content-Length", str(len(self.payload)))
            self.end_headers()
            self.wfile.write(self.payload)

        def log_message(self, *_args):
            pass

    httpd = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=httpd.serve_forever, daemon=True)
    worker.start()
    uri = f"http://127.0.0.1:{httpd.server_port}/v1/audio/speech"
    try:
        result = measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.")
        assert result["pcm_bytes"] == 4800
        assert result["sample_rate_hz"] == 24000
        assert result["audio_seconds"] == 0.1
        Handler.sample_rate = 999999
        try:
            measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.")
            raise AssertionError("Invalid sample rate was accepted")
        except ValueError:
            pass
        Handler.sample_rate = 24000
        Handler.payload = b"\x00"
        try:
            measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.")
            raise AssertionError("Odd PCM frame was accepted")
        except ValueError:
            pass
        try:
            measure("http://example.com/v1/audio/speech", "m", "v", "t")
            raise AssertionError("Remote endpoint was accepted")
        except ValueError:
            pass
    finally:
        httpd.shutdown()
        httpd.server_close()
        worker.join(timeout=3)
    print("voice_benchmark local protocol self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--model", default="qwen3-tts-0.6b")
    parser.add_argument("--voice", default="vivian")
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--output", type=Path,
                        help="Optional JSON-only result file; never saves generated audio")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    safe = re.compile(r"^[A-Za-z0-9_./-]{1,80}$")
    if not safe.fullmatch(args.model) or not safe.fullmatch(args.voice):
        parser.error("Model and voice must be short safe local identifiers")
    if not 1 <= args.samples <= 40:
        parser.error("--samples must be between 1 and 40")
    results = []
    for i in range(args.samples):
        prompt = PROMPTS[i % len(PROMPTS)]
        try:
            result = measure(ENDPOINT, args.model, args.voice, prompt)
        except (OSError, URLError, HTTPError, ValueError) as exc:
            parser.error(f"Local TTS sample {i + 1} failed: {type(exc).__name__}: {exc}")
        result["sample_number"] = i + 1
        results.append(result)
    first = [r["first_http_pcm_byte_ms"] for r in results]
    report = {
        "note": "HTTP first PCM byte is NOT the first acoustic output.",
        "model": args.model, "voice": args.voice, "samples": len(results),
        "p50_first_http_pcm_byte_ms": round(statistics.median(first), 2),
        "p95_first_http_pcm_byte_ms": percentile95(first),
        "mean_audio_seconds_per_wall_second": round(statistics.mean(
            r["audio_seconds_per_wall_second"] for r in results), 2),
        "trials": results,
    }
    content = json.dumps(report, indent=2, ensure_ascii=False)
    if args.output:
        args.output.write_text(content + "\n", encoding="utf-8")
    print(content)


if __name__ == "__main__":
    main()
