#!/usr/bin/env python3
"""One-shot, local-only Windows speech.cpp acceptance for an approved Ember bundle.

Default: read-only SHA-256/license/header preflight. Explicit --run starts a
bundled native speech.exe, tests actual GGUF/PCM HTTP inference, stops it and
reports signal/latency metrics without storing prompts or audio. This is NOT a
blind listening test, network isolation proof or final release approval.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

from offline_bundle import (
    SUPPORTED_VOICE_MODELS, verify, verify_gguf_header,
    verify_windows_amd64_pe,
)
from localhost_owner import listening_owner_pids, require_owned_speech_listener
from voice_benchmark import ENDPOINT, PROMPTS, measure

PORT = 43822
HEALTH = "http://127.0.0.1:43822/health"
MODELS = {
    "qwen3-tts-0.6b": SUPPORTED_VOICE_MODELS[0],
    "qwen3-tts-1.7b": SUPPORTED_VOICE_MODELS[1],
}


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


HTTP = build_opener(ProxyHandler({}), NoRedirect())


def speech_owners():
    """Check the same exact IPv4 listener Ember verifies, not a generic port."""
    done = subprocess.run(
        ["netstat", "-ano", "-p", "tcp"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, timeout=10, check=False)
    if done.returncode:
        raise RuntimeError("Unable to inspect local speech port owner")
    return listening_owner_pids(done.stdout.decode("ascii", "ignore"), PORT)


def startup_environment():
    """Do not leak shell API credentials or permit speech.cpp update checks."""
    env = os.environ.copy()
    for name in list(env):
        upper = name.upper()
        if (any(term in upper for term in
                ("TOKEN", "SECRET", "PASSWORD", "CREDENTIAL", "API_KEY")) or
                upper.startswith(("AWS_", "GITHUB_", "OPENAI_", "GOOGLE_",
                                  "AZURE_"))):
            env.pop(name, None)
    env["SPEECH_NO_UPDATE_CHECK"] = "1"
    env["HF_HUB_OFFLINE"] = "1"
    env["TRANSFORMERS_OFFLINE"] = "1"
    return env


def model_path(root, alias):
    if alias not in MODELS:
        raise ValueError("Unknown native speech alias")
    path = root / MODELS[alias]
    if not path.is_file() or path.is_symlink():
        raise ValueError("Approved speech GGUF is missing or linked")
    verify_gguf_header(path, MODELS[alias])
    return path


def wait_for_speech(proc, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("speech.exe stopped before becoming ready")
        owners = speech_owners()
        if owners and owners != {proc.pid}:
            raise RuntimeError("Untrusted process owns local speech port")
        if owners == {proc.pid}:
            require_owned_speech_listener(proc.pid, PORT)
            try:
                with HTTP.open(Request(HEALTH, method="GET"), timeout=3) as reply:
                    body = reply.read(4097)
                    if reply.status == 200 and len(body) <= 4096:
                        require_owned_speech_listener(proc.pid, PORT)
                        return
                    if len(body) > 4096:
                        raise RuntimeError("Oversized speech health response")
            except (HTTPError, URLError, TimeoutError):
                pass
        time.sleep(1)
    raise RuntimeError("speech.cpp did not become ready in the bounded time")


def probe(root, alias, device, voice, count, seconds):
    if speech_owners():
        raise RuntimeError("Port 43822 is already occupied; stop that process first")
    executable = root / "engines/speech/speech.exe"
    weights = model_path(root, alias)
    verify_windows_amd64_pe(executable, "engines/speech/speech.exe")
    args = [str(executable), "serve", str(weights), "--host", "127.0.0.1",
            "--port", str(PORT), "--device", device, "--threads", "4"]
    proc = subprocess.Popen(
        args, cwd=str(executable.parent), env=startup_environment(),
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    try:
        wait_for_speech(proc, seconds)
        trials = []
        for i in range(count):
            if proc.poll() is not None:
                raise RuntimeError("Native speech process exited between trials")
            sample = measure(ENDPOINT, alias, voice, PROMPTS[i % len(PROMPTS)],
                             "natural", omit_model_id=True, seed=i + 1,
                             expected_pid=proc.pid)
            if (not sample["digital_voice_detected"] or
                    sample["audio_seconds"] < 0.2):
                raise RuntimeError("Native speech returned silence or short PCM")
            # Only numeric diagnostics are retained, not the synthesized audio.
            trials.append({"trial": i + 1, **sample})
        return {
            "native_speech_http_pcm": "PASS",
            "model_alias": alias,
            "device_request": device,
            "voice": voice,
            "samples": count,
            "trials": trials,
            "release_approved": False,
            "caveats": ("PCM signal checks are not acoustic naturalness, "
                        "audible playback, Windows microphone, GPU contention, "
                        "or third-party binary network-isolation tests.")
        }
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=8)


def self_test():
    fixture = ("TCP    127.0.0.1:43822    0.0.0.0:0    LISTENING    2480\n"
               "TCP    127.0.0.1:43822    127.0.0.1:5100    ESTABLISHED    2480\n"
               "TCP    0.0.0.0:43822     0.0.0.0:0    LISTENING    9999\n"
               "TCP    127.0.0.1:43821    0.0.0.0:0    LISTENING    5000\n")
    assert listening_owner_pids(fixture, PORT) == {2480}
    assert listening_owner_pids("", PORT) == set()
    env = startup_environment()
    assert env["SPEECH_NO_UPDATE_CHECK"] == "1"
    assert env["HF_HUB_OFFLINE"] == "1"
    assert set(MODELS) == {"qwen3-tts-0.6b", "qwen3-tts-1.7b"}
    try:
        model_path(Path("."), "../unlisted")
        raise AssertionError("Unapproved model alias passed")
    except ValueError:
        pass
    assert hashlib.sha256(PROMPTS[0].encode()).hexdigest()
    print("Native speech acceptance preflight self-tests passed; no model launched")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path,
                        help="Approved, assembled native Windows Ember bundle")
    parser.add_argument("--run", action="store_true",
                        help="Explicitly launch local speech.exe and test real PCM")
    parser.add_argument("--model", choices=tuple(MODELS), default="qwen3-tts-0.6b")
    parser.add_argument("--device", choices=("auto", "cpu", "gpu"), default="cpu")
    parser.add_argument("--voice", default="vivian")
    parser.add_argument("--samples", type=int, default=2)
    parser.add_argument("--startup-seconds", type=int, default=180)
    parser.add_argument("--report", type=Path,
                        help="New JSON metrics only; no audio or private text")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.root is None:
        parser.error("--root required unless --self-test")
    if (not 1 <= args.samples <= 8 or not 30 <= args.startup_seconds <= 600 or
            not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", args.voice)):
        parser.error("Invalid sample count, startup timeout or voice identifier")
    if args.report is not None and args.report.exists():
        parser.error("Refusing to overwrite an existing acceptance report")
    root = args.root.resolve(strict=True)
    verify(root, root / "offline-assets.json", approved=True, require_voice=True)
    if not (root / "Ember.exe").is_file():
        raise ValueError("Missing native Ember.exe in the assembled payload")
    model_path(root, args.model)
    print("Approved complete offline manifest and speech model header: PASS")
    if not args.run:
        print("PREFLIGHT ONLY. No models, microphone or executable launched.")
        return
    if os.name != "nt":
        parser.error("--run requires native Windows and Windows netstat PID checks")
    report = probe(root, args.model, args.device, args.voice,
                   args.samples, args.startup_seconds)
    output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.report is not None:
        # Exclusive file creation means a later attempt cannot erase a baseline.
        with args.report.open("x", encoding="utf-8") as stream:
            stream.write(output)
    print(output)
    print("Live PCM verified; human listening and clean-PC acceptance remain OPEN.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired,
            HTTPError, URLError, json.JSONDecodeError) as exc:
        sys.exit("Native speech acceptance failed: " +
                 type(exc).__name__ + ": " + str(exc))
