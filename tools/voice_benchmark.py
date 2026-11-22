#!/usr/bin/env python3
"""Local-only voice latency probe. Does not record, save or play any audio."""
import argparse
import hashlib
import json
import math
import re
import statistics
import subprocess
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

from localhost_owner import require_owned_speech_listener

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


def pcm_signal_metrics(pcm, sample_rate):
    """Measure electrical PCM health only. Never infer audible naturalness.

    Per-trial PCM lives in memory until the response has finished; calculating
    metrics afterward prevents the Python analysis loop from delaying the HTTP
    synthesis-duration measurement.
    """
    if (not 8000 <= sample_rate <= 96000 or len(pcm) < 2 or
            len(pcm) % 2 or len(pcm) > MAX_BYTES):
        raise ValueError("PCM signal has invalid rate, length or byte budget")
    samples = len(pcm) // 2
    first = last = None
    peak = clipped = quiet = longest_quiet = current_quiet = 0
    sum_values = sum_squares = 0
    for i, (sample,) in enumerate(
            struct.iter_unpack("<h", memoryview(pcm))):
        mag = abs(sample)
        peak = max(peak, mag)
        sum_values += sample
        sum_squares += sample * sample
        if mag >= 32760:
            clipped += 1
        if mag < ONSET_THRESHOLD:
            quiet += 1
            current_quiet += 1
            longest_quiet = max(longest_quiet, current_quiet)
        else:
            current_quiet = 0
            if first is None:
                first = i
            last = i
    rms = math.sqrt(sum_squares / samples) / 32768
    peak_level = peak / 32768
    def dbfs(level):
        return round(20 * math.log10(level), 2) if level > 0 else None
    return {
        "digital_onset_ms": (
            round(first * 1000 / sample_rate, 2) if first is not None else None),
        "leading_low_level_pcm_ms": (
            round(first * 1000 / sample_rate, 2) if first is not None else None),
        "trailing_low_level_pcm_ms": (
            round((samples - 1 - last) * 1000 / sample_rate, 2)
            if last is not None else None),
        "digital_voice_detected": first is not None,
        "quiet_sample_percent": round(quiet * 100 / samples, 2),
        "longest_quiet_span_ms": round(longest_quiet * 1000 / sample_rate, 2),
        "clipped_sample_percent": round(clipped * 100 / samples, 4),
        "peak_dbfs": dbfs(peak_level),
        "rms_dbfs": dbfs(rms),
        "dc_offset_percent_fullscale": round(
            abs(sum_values / samples) * 100 / 32768, 4),
    }


class RejectRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None  # never forward locally generated spoken text to a remote host


OPENER = build_opener(ProxyHandler({}), RejectRedirect())


def percentile95(values):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(0.95 * len(ordered)) - 1)]


def measure(endpoint, model, voice, prompt, delivery="natural", omit_model_id=False,
            seed=1, *, expected_pid=None, _fixture=False):
    u = urlsplit(endpoint)
    if (u.scheme != "http" or u.hostname != "127.0.0.1" or
            u.port is None or u.username is not None or
            u.password is not None or u.query or u.fragment or
            u.path != "/v1/audio/speech"):
        raise ValueError("Only loopback HTTP TTS endpoints are allowed")
    if _fixture:
        if u.port == 43822:
            raise ValueError("Fixture bypass is forbidden on live speech port")
    else:
        if endpoint != ENDPOINT:
            raise ValueError("Real TTS benchmark requires Ember's exact local port")
        # Fail before sending even the built-in English test prompts to an
        # unknown local HTTP listener on the default Ember speech port.
        require_owned_speech_listener(expected_pid, 43822)
    if delivery not in DELIVERIES or (delivery != "natural" and
                                     model != "qwen3-tts-1.7b"):
        raise ValueError("Delivery instructions require native Qwen3-TTS 1.7B")
    if not isinstance(seed, int) or not 0 <= seed <= 2**31 - 1:
        raise ValueError("Reproducible model seed out of range")
    body = {
        "seed": seed, "model": model, "voice": voice, "input": prompt,
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
        sample_bytes = bytearray(first)
        while True:
            chunk = response.read(65536)
            if not chunk:
                break
            if len(sample_bytes) + len(chunk) > MAX_BYTES:
                raise ValueError("PCM exceeded the 12 MiB local safety budget")
            sample_bytes.extend(chunk)
    # Stop latency clock BEFORE any CPU-heavy signal analysis. Each trial's
    # PCM is transient in RAM; results contain numeric telemetry only.
    total_ms = (time.perf_counter() - started) * 1000
    if not _fixture:
        # An unexpected speech.exe shutdown/rebind invalidates the trial.
        require_owned_speech_listener(expected_pid, 43822)
    count = len(sample_bytes)
    if count % 2 or count < 2:
        raise ValueError("PCM16 response ended with incomplete sample frames")
    duration = count / (2 * rate)
    health = pcm_signal_metrics(sample_bytes, rate)
    return {
        "sample_rate_hz": rate,
        "pcm_bytes": count,
        "audio_seconds": round(duration, 3),
        "first_http_pcm_byte_ms": round(first_ms, 2),
        **health,
        "total_http_synthesis_ms": round(total_ms, 2),
        "audio_seconds_per_wall_second": round(duration / (total_ms / 1000), 2),
    }


def self_test():
    class Handler(BaseHTTPRequestHandler):
        sample_rate = 24000
        payload = b"\x00\x00" * 2400
        omit_model_id = False

        def do_POST(self):
            assert self.path == "/v1/audio/speech"
            n = int(self.headers["Content-Length"])
            body = json.loads(self.rfile.read(n))
            assert body["response_format"] == "pcm"
            assert body["stream_format"] == "audio"
            assert body["language"] == "en"
            assert body["seed"] in (1, 2, 3)
            if self.omit_model_id:
                assert "model" not in body
            else:
                assert "model" in body
            if body.get("model") == "qwen3-tts-1.7b":
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
        try:
            measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.")
            raise AssertionError("Unverified local speech listener was accepted")
        except ValueError:
            pass
        try:
            measure(ENDPOINT, "qwen3-tts-0.6b", "vivian", "Hello.",
                    _fixture=True)
            raise AssertionError("Mock bypass reached default speech port")
        except ValueError:
            pass
        result = measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.",
                         _fixture=True)
        assert result["pcm_bytes"] == 4800
        Handler.omit_model_id = True
        result_local = measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.",
                               omit_model_id=True, _fixture=True)
        assert result_local["pcm_bytes"] == 4800
        Handler.omit_model_id = False
        assert result["sample_rate_hz"] == 24000
        assert result["audio_seconds"] == 0.1
        assert result["leading_low_level_pcm_ms"] is None
        assert result["digital_voice_detected"] is False
        assert result["rms_dbfs"] is None
        assert result["peak_dbfs"] is None
        assert result["quiet_sample_percent"] == 100
        assert StreamingPcmOnset().first_significant_sample is None
        onset = StreamingPcmOnset()
        onset.feed(b"\x00")
        onset.feed(b"\x00" * 479 + struct.pack("<h", 900))
        assert onset.first_significant_sample == 240
        Handler.payload = b"\x00\x00" * 1200 + struct.pack("<h", 1000) * 1200
        expressive = measure(uri, "qwen3-tts-1.7b", "serena", "Warm hello.",
                             delivery="warm", _fixture=True)
        assert expressive["leading_low_level_pcm_ms"] == 50
        assert expressive["digital_voice_detected"] is True
        assert expressive["quiet_sample_percent"] == 50
        assert expressive["clipped_sample_percent"] == 0
        assert expressive["trailing_low_level_pcm_ms"] == 0
        assert expressive["peak_dbfs"] is not None
        clipped = b"\x00\x00" * 100 + struct.pack("<h", 32767) * 100
        clipped += b"\x00\x00" * 100
        metrics = pcm_signal_metrics(clipped, 24000)
        assert metrics["clipped_sample_percent"] == round(100 / 300 * 100, 4)
        assert metrics["digital_onset_ms"] == round(100 * 1000 / 24000, 2)
        assert metrics["trailing_low_level_pcm_ms"] == round(100 * 1000 / 24000, 2)
        assert metrics["longest_quiet_span_ms"] == round(100 * 1000 / 24000, 2)
        try:
            pcm_signal_metrics(b"\x00", 24000)
            raise AssertionError("Odd PCM frame incorrectly accepted")
        except ValueError:
            pass
        Handler.payload = b"\x00\x00" * 2400
        try:
            measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.", delivery="warm", _fixture=True)
            raise AssertionError("0.6B delivery instructions were accepted")
        except ValueError:
            pass
        Handler.sample_rate = 999999
        try:
            measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.", _fixture=True)
            raise AssertionError("Invalid sample rate was accepted")
        except ValueError:
            pass
        Handler.sample_rate = 24000
        Handler.payload = b"\x00"
        try:
            measure(uri, "qwen3-tts-0.6b", "vivian", "Hello.", _fixture=True)
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
    parser.add_argument("--speech-pid", type=int,
                        help="Required Windows PID of the trusted speech.exe "
                             "listener on 127.0.0.1:43822")
    parser.add_argument("--omit-model-id", action="store_true",
                        help="For speech.cpp started from a local GGUF path; no catalog alias")
    parser.add_argument("--delivery", choices=tuple(DELIVERIES),
                        default="natural", help="Style controls are supported by 1.7B only")
    parser.add_argument("--samples", type=int, default=8)
    parser.add_argument("--seed-base", type=int, default=1,
                        help="Reproducible seed cycle: base, base+1, base+2")
    parser.add_argument("--environment-label", default="unknown",
                        help="Manual comparable hardware/load label; no auto PC fingerprint")
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
    if not args.speech_pid or args.speech_pid <= 0:
        parser.error("--speech-pid is mandatory for real native TTS comparisons")
    try:
        require_owned_speech_listener(args.speech_pid, 43822)
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        parser.error("Trusted local speech.cpp PID check failed: " + str(exc))
    if not 0 <= args.seed_base <= 2**31 - 4:
        parser.error("Seed base must be 0..2147483644")
    if not re.fullmatch(r"[A-Za-z0-9_.-]{2,64}", args.environment_label):
        parser.error("Environment label must be 2..64 safe ASCII characters")
    results = []
    for i in range(args.samples):
        prompt = PROMPTS[i % len(PROMPTS)]
        try:
            seed = args.seed_base + i % 3
            result = measure(ENDPOINT, args.model, args.voice, prompt,
                             args.delivery, omit_model_id=args.omit_model_id,
                             seed=seed, expected_pid=args.speech_pid)
        except (OSError, URLError, HTTPError, ValueError) as exc:
            parser.error(f"Local TTS sample {i + 1} failed: {type(exc).__name__}: {exc}")
        result["sample_number"] = i + 1
        result["prompt_index"] = i % len(PROMPTS)  # never persist prompt text
        result["seed"] = seed
        results.append(result)
    first = [r["first_http_pcm_byte_ms"] for r in results]
    leading = [r["leading_low_level_pcm_ms"] for r in results
               if r["leading_low_level_pcm_ms"] is not None]
    clipped = [r["clipped_sample_percent"] for r in results]
    leading_silence = [r["leading_low_level_pcm_ms"] for r in results
                       if r["leading_low_level_pcm_ms"] is not None]
    no_signal = sum(not r["digital_voice_detected"] for r in results)
    report = {
        "note": "All PCM measurements are signal diagnostics, NOT acoustic onset, naturalness or MOS. They do not test output-device buffering. HTTP time ends before PCM analysis.",
        "benchmark_schema_version": 2,
        "prompt_set_sha256": hashlib.sha256(
            "\n".join(PROMPTS).encode("utf-8")).hexdigest(),
        "environment_label": args.environment_label,
        "model": args.model, "voice": args.voice, "delivery": args.delivery,
        "samples": len(results),
        "p50_leading_low_level_pcm_ms": (
            round(statistics.median(leading), 2) if leading else None),
        "p95_leading_low_level_pcm_ms": (
            round(percentile95(leading_silence), 2) if leading_silence else None),
        "p95_clipped_sample_percent": round(percentile95(clipped), 4),
        "no_digital_voice_trials": no_signal,
        "p50_first_http_pcm_byte_ms": round(statistics.median(first), 2),
        "p95_first_http_pcm_byte_ms": percentile95(first),
        "mean_audio_seconds_per_wall_second": round(statistics.mean(
            r["audio_seconds_per_wall_second"] for r in results), 2),
        "trials": results,
    }
    content = json.dumps(report, indent=2, ensure_ascii=False)
    if args.output:
        # A new A/B run must never silently erase the earlier numeric baseline.
        with args.output.open("x", encoding="utf-8") as stream:
            stream.write(content + "\n")
    print(content)


if __name__ == "__main__":
    main()
