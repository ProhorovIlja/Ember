#!/usr/bin/env python3
"""Local-only voice latency probe. Does not record, save or play any audio."""
import argparse
import json
import math
import re
import statistics
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

ENDPOINT = "http://127.0.0.1:43822/v1/audio/speech"
PROMPTS = (
    "The train is leaving in fifteen minutes. Would you like me to check the next connection?",
    "I thought you had already left. Well, that is a wonderful surprise!",
    "The graphics card has twelve gigabytes of video memory. Let us measure it carefully.",
    "Wait, is that really what you meant? Tell me more.",
)
MAX_BYTES = 12 * 1024 * 1024
ONSET_THRESHOLD = 256  # -42 dBFS peak; a signal metric, not a speaker measurement
DELIVERIES = {
    "natural": "",
    "warm": "Speak warmly and naturally, with subtle expressive inflection and relaxed conversational rhythm.",
    "soft": "Speak gently and softly, with clear diction and a calm, intimate tone.",
    "bright": "Speak with bright curiosity, lively natural emphasis and playful energy, without overacting.",
    "story": "Narrate vividly with controlled dramatic expression, clear dialogue and natural changes of emphasis.",
}


class StreamingPcmOnset:
    """Track first nontrivially loud PCM16 sample without persisting sound."""

    def __init__(self):
        self.carry = b""
        self.count = 0
        self.first_significant_sample = None

    def feed(self, chunk):
        data = self.carry + chunk
        end = len(data) & ~1
        if self.first_significant_sample is None:
            for i, (sample,) in enumerate(struct.iter_unpack("<h", memoryview(data)[:end])):
                if abs(sample) >= ONSET_THRESHOLD:
                    self.first_significant_sample = self.count + i
                    break
        self.count += end // 2
        self.carry = data[end:]

    def finish(self):
        if self.carry:
            raise ValueError("PCM16 stream ended in a partial 16-bit sample")

class RejectRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None  # never forward locally generated spoken text to a remote host


OPENER = build_opener(ProxyHandler({}), RejectRedirect())


def percentile95(values):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)]


def measure(endpoint, model, voice, prompt, delivery="natural", omit_model_id=False):
    u = urlsplit(endpoint)
    if (u.scheme != "http" or u.hostname != "127.0.0.1" or
            u.port is None or u.username is not None or
            u.password is not None or u.query or u.fragment or
            u.path != "/v1/audio/speech"):
        raise ValueError("Only loopback HTTP TTS endpoints are allowed")
    if delivery not in DELIVERIES or (delivery != "natural" and
                                     model != "qwen3-tts-1.7b"):
        raise ValueError("Delivery instructions require native Qwen3-TTS 1.7B")
    body = {
        "model": model, "voice": voice, "input": prompt,
        "response_format": "pcm", "stream_format": "audio", "language": "en",
    }
    if DELIVERIES[delivery]:
        body["instructions"] = DELIVERIES[delivery]
    if omit_model_id:
        body.pop("model")  # one local file-backed TTS model selected by the server
    payload = json.dumps(body, separators=(",", ":")).encode("utf-8")
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
        onset = StreamingPcmOnset()
        onset.feed(first)
        while True:
            chunk = response.read(65536)
            if not chunk:
                break
            count += len(chunk)
            if count > MAX_BYTES:
                raise ValueError("PCM exceeded the 12 MiB local safety budget")
            if onset.first_significant_sample is None:
                onset.feed(chunk)
    total_ms = (time.perf_counter() - started) * 1000
    if count % 2 or count < 2:
        raise ValueError("PCM16 response ended with incomplete sample frames")
    duration = count / (2 * rate)
    return {
        "sample_rate_hz": rate,
        "pcm_bytes": count,
        "audio_seconds": round(duration, 3),
        "first_http_pcm_byte_ms": round(first_ms, 2),
        "leading_low_level_pcm_ms": (
            round(onset.first_significant_sample * 1000 / rate, 2)
            if onset.first_significant_sample is not None else None),
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
            assert body["language"] == "en"
            if body["model"] == "qwen3-tts-1.7b":
                assert "warmly" in body["instructions"]
            else:
                assert "instructions" not in body
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
        result_local = measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.",
                               omit_model_id=True)
        assert result_local["pcm_bytes"] == 4800
        assert result["sample_rate_hz"] == 24000
        assert result["audio_seconds"] == 0.1
        assert result["leading_low_level_pcm_ms"] is None
        assert StreamingPcmOnset().first_significant_sample is None
        onset = StreamingPcmOnset()
        onset.feed(b"\x00")
        onset.feed(b"\x00" * 479 + struct.pack("<h", 900))
        assert onset.first_significant_sample == 240
        Handler.payload = b"\x00\x00" * 1200 + struct.pack("<h", 1000) * 1200
        expressive = measure(uri, "qwen3-tts-1.7b", "serena", "Warm hello.",
                             delivery="warm")
        assert expressive["leading_low_level_pcm_ms"] == 50
        Handler.payload = b"\x00\x00" * 2400
        try:
            measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.", delivery="warm")
            raise AssertionError("0.6B delivery instructions were accepted")
        except ValueError:
            pass
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
    parser.add_argument("--omit-model-id", action="store_true",
                        help="For speech.cpp started from a local GGUF path; no catalog alias")
    parser.add_argument("--delivery", choices=tuple(DELIVERIES),
                        default="natural", help="Style controls are supported by 1.7B only")
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
            result = measure(ENDPOINT, args.model, args.voice, prompt,
                             args.delivery, omit_model_id=args.omit_model_id)
        except (OSError, URLError, HTTPError, ValueError) as exc:
            parser.error(f"Local TTS sample {i + 1} failed: {type(exc).__name__}: {exc}")
        result["sample_number"] = i + 1
        results.append(result)
    first = [r["first_http_pcm_byte_ms"] for r in results]
    leading = [r["leading_low_level_pcm_ms"] for r in results
               if r["leading_low_level_pcm_ms"] is not None]
    report = {
        "note": "First HTTP PCM byte and first digital sample >=256 do NOT measure actual acoustic output.",
        "model": args.model, "voice": args.voice, "delivery": args.delivery,
        "samples": len(results),
        "p50_leading_low_level_pcm_ms": (
            round(statistics.median(leading), 2) if leading else None),
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
