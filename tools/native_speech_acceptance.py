#!/usr/bin/env python3
"""One-shot, local-only Windows speech.cpp acceptance for an approved Ember bundle.

Default: read-only SHA-256/license/header preflight. Explicit --run starts a
bundled native speech.exe, tests GGUF/PCM HTTP inference and stops it.
Optional --listen (only with --run) plays validated PCM samples on Windows
synchronously from memory, without writing sound files. Audible playback is
not a blinded listening assessment or automatic quality certification.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import struct
import time
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

from offline_bundle import (
    SUPPORTED_VOICE_MODELS, verify, verify_gguf_header,
    verify_windows_amd64_pe,
)
from localhost_owner import listening_owner_pids, require_owned_speech_listener
from voice_benchmark import ENDPOINT, PROMPTS, measure
from private_worker_env import private_worker_environment
from native_gguf_metadata import model_identity, self_test as native_gguf_self_test

PORT = 43822
HEALTH = "http://127.0.0.1:43822/health"
INVENTORY = "http://127.0.0.1:43822/v1/models"
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
    """Never leak shell secrets to the third-party native speech worker."""
    return private_worker_environment()


def model_path(root, alias):
    if alias not in MODELS:
        raise ValueError("Unknown native speech alias")
    path = root / MODELS[alias]
    if not path.is_file() or path.is_symlink():
        raise ValueError("Approved speech GGUF is missing or linked")
    verify_gguf_header(path, MODELS[alias])
    return path


def valid_health_reply(status, content_type, body):
    """The documented speech.cpp readiness contract, not a generic HTTP 200."""
    if (status != 200 or not isinstance(body, bytes) or
            not 0 < len(body) <= 4096 or
            content_type.split(";", 1)[0].strip().lower() != "application/json"):
        return False
    try:
        payload = json.loads(body.decode("utf-8", "strict"))
    except (ValueError, UnicodeDecodeError):
        return False
    return isinstance(payload, dict) and payload.get("status") == "ok"


def valid_inventory_reply(status, content_type, body, alias, voice):
    """Verify loaded model identity and selected English female voice."""
    if (status != 200 or not isinstance(body, bytes) or
            not 0 < len(body) <= 65536 or
            content_type.split(";", 1)[0].strip().lower() != "application/json" or
            alias not in MODELS or not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", voice)):
        return False
    try:
        doc = json.loads(body.decode("utf-8", "strict"))
    except (UnicodeError, ValueError):
        return False
    if not isinstance(doc, dict) or doc.get("object") != "list":
        return False
    rows = doc.get("data")
    if not isinstance(rows, list) or not 1 <= len(rows) <= 8:
        return False
    wanted = "0.6B" if alias.endswith("0.6b") else "1.7B"
    matched = 0
    for row in rows:
        if (not isinstance(row, dict) or
                not isinstance(row.get("id"), str) or not row["id"] or
                not isinstance(row.get("speech"), dict)):
            return False
        info = row["speech"]
        task = info.get("task")
        if task == "synthesis":
            if (info.get("architecture") != "qwen3-tts" or
                    str(info.get("size_label", "")).lower() != wanted.lower() or
                    not isinstance(info.get("languages"), list) or
                    "en" not in info["languages"] or
                    not isinstance(info.get("voices"), list) or
                    not 1 <= len(info["voices"]) <= 128):
                return False
            seen = set()
            selected_female = False
            for entry in info["voices"]:
                if not isinstance(entry, dict):
                    return False
                name = entry.get("name")
                language = entry.get("language")
                gender = entry.get("gender")
                if (not isinstance(name, str) or
                        not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", name) or
                        name in seen or
                        not isinstance(language, str) or
                        language not in info["languages"] or
                        gender not in ("female", "male")):
                    return False
                seen.add(name)
                if name == voice and language == "en" and gender == "female":
                    selected_female = True
            if not selected_female:
                return False
            matched += 1
    return matched == 1


def wait_for_speech(proc, seconds, alias, voice):
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
                    if valid_health_reply(
                            reply.status, reply.headers.get("Content-Type", ""),
                            body):
                        require_owned_speech_listener(proc.pid, PORT)
                        with HTTP.open(Request(INVENTORY, method="GET"),
                                       timeout=3) as catalog:
                            metadata = catalog.read(65537)
                            if not valid_inventory_reply(
                                    catalog.status,
                                    catalog.headers.get("Content-Type", ""),
                                    metadata, alias, voice):
                                raise RuntimeError(
                                    "Loaded native GGUF model, size, English "
                                    "speaker or inventory JSON is incompatible")
                        require_owned_speech_listener(proc.pid, PORT)
                        return
                    if len(body) > 4096:
                        raise RuntimeError("Oversized speech health response")
            except (HTTPError, URLError, TimeoutError):
                pass
        time.sleep(1)
    raise RuntimeError("speech.cpp did not become ready in the bounded time")


def pcm16_mono_wav(pcm, sample_rate):
    """Wrap validated, bounded PCM in an in-memory canonical RIFF/WAVE buffer."""
    if (not isinstance(pcm, bytes) or not 0 < len(pcm) <= 12 * 1024 * 1024 or
            len(pcm) % 2 or type(sample_rate) is not int or
            not 8000 <= sample_rate <= 96000):
        raise ValueError("Cannot play malformed PCM16 mono response")
    return (b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
            + b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, sample_rate,
                                     2 * sample_rate, 2, 16)
            + b"data" + struct.pack("<I", len(pcm)) + pcm)


def play_pcm_locally(pcm, sample_rate):
    """Blocking Windows playback: no temp WAV, player subprocess or network."""
    if os.name != "nt":
        raise RuntimeError("Listening requires a native Windows audio output")
    import winsound
    # Winsound's SND_MEMORY is synchronous; do not combine it with SND_ASYNC.
    winsound.PlaySound(pcm16_mono_wav(pcm, sample_rate),
                       winsound.SND_MEMORY | winsound.SND_SYNC |
                       winsound.SND_NODEFAULT)


def probe(root, alias, device, voice, count, seconds, listen=False):
    if speech_owners():
        raise RuntimeError("Port 43822 is already occupied; stop that process first")
    executable = root / "engines/speech/speech.exe"
    weights = model_path(root, alias)
    model_identity(weights, alias, selected_voice=voice)
    verify_windows_amd64_pe(executable, "engines/speech/speech.exe")
    args = [str(executable), "serve", str(weights), "--host", "127.0.0.1",
            "--port", str(PORT), "--device", device, "--threads", "4"]
    proc = subprocess.Popen(
        args, cwd=str(executable.parent), env=startup_environment(),
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    try:
        wait_for_speech(proc, seconds, alias, voice)
        trials = []
        for i in range(count):
            if proc.poll() is not None:
                raise RuntimeError("Native speech process exited between trials")
            if listen:
                print(f"Manual voice listening sample {i + 1}/{count} "
                      f"({alias}, {voice}) through Windows default output.",
                      flush=True)
            sample = measure(ENDPOINT, alias, voice, PROMPTS[i % len(PROMPTS)],
                             "natural", omit_model_id=True, seed=i + 1,
                             expected_pid=proc.pid,
                             on_pcm=play_pcm_locally if listen else None)
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
            "human_audio_playback_requested": bool(listen),
            "release_approved": False,
            "caveats": ("Playback only confirms a listening attempt, not "
                        "naturalness or voice preference. Windows microphone, "
                        "GPU contention and third-party binary network isolation "
                        "remain untested.")
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
    native_gguf_self_test()
    fixture = ("TCP    127.0.0.1:43822    0.0.0.0:0    LISTENING    2480\n"
               "TCP    127.0.0.1:43822    127.0.0.1:5100    ESTABLISHED    2480\n"
               "TCP    0.0.0.0:43822     0.0.0.0:0    LISTENING    9999\n"
               "TCP    127.0.0.1:43821    0.0.0.0:0    LISTENING    5000\n")
    assert listening_owner_pids(fixture, PORT) == {2480}
    assert listening_owner_pids("", PORT) == set()
    assert valid_health_reply(200, "application/json", b'{"status":"ok"}')
    assert valid_health_reply(
        200, "application/json; charset=utf-8", b'{"status":"ok"}')
    assert not valid_health_reply(200, "application/json", b'{"status":"loading"}')
    assert not valid_health_reply(200, "application/json", b'{}')
    assert not valid_health_reply(200, "text/html", b'{"status":"ok"}')
    assert not valid_health_reply(503, "application/json", b'{"status":"ok"}')
    assert not valid_health_reply(200, "application/json", b'x' * 4097)
    assert not valid_health_reply(200, "application/json", b'{')
    row = {"id": "local-qwen-gguf", "object": "model",
           "speech": {"task": "synthesis", "architecture": "qwen3-tts",
                      "size_label": "0.6B", "languages": ["en", "ru"],
                      "voices": [{"name": "vivian", "language": "en",
                                  "gender": "female"},
                                 {"name": "ryan", "language": "en",
                                  "gender": "male"}]}}
    sample = json.dumps({"object": "list", "data": [row]}).encode("utf-8")
    assert valid_inventory_reply(200, "application/json", sample,
                                 "qwen3-tts-0.6b", "vivian")
    assert not valid_inventory_reply(200, "application/json", sample,
                                     "qwen3-tts-1.7b", "vivian")
    assert not valid_inventory_reply(200, "application/json", sample,
                                     "qwen3-tts-0.6b", "serena")
    assert not valid_inventory_reply(200, "application/json", sample,
                                     "qwen3-tts-0.6b", "ryan")
    for extra in (
            {"name": "vivian", "language": "en", "gender": "male"},
            {"name": "vivian", "language": "en", "gender": "female"},
            {"name": "other", "language": "ja", "gender": "female"},
            {"name": "other", "language": "en", "gender": "unknown"},
            "invalid"):
        inconsistent = json.loads(sample)
        inconsistent["data"][0]["speech"]["voices"].append(extra)
        assert not valid_inventory_reply(
            200, "application/json", json.dumps(inconsistent).encode(),
            "qwen3-tts-0.6b", "vivian")
    no_gender = json.loads(sample)
    del no_gender["data"][0]["speech"]["voices"][0]["gender"]
    assert not valid_inventory_reply(
        200, "application/json", json.dumps(no_gender).encode(),
        "qwen3-tts-0.6b", "vivian")
    assert not valid_inventory_reply(200, "text/html", sample,
                                     "qwen3-tts-0.6b", "vivian")
    assert not valid_inventory_reply(200, "application/json", b"{}",
                                     "qwen3-tts-0.6b", "vivian")
    assert not valid_inventory_reply(200, "application/json", b"x" * 65537,
                                     "qwen3-tts-0.6b", "vivian")
    broken = dict(row)
    broken["speech"] = dict(row["speech"], task="recognition")
    assert not valid_inventory_reply(
        200, "application/json",
        json.dumps({"object": "list", "data": [broken]}).encode("utf-8"),
        "qwen3-tts-0.6b", "vivian")
    wav = pcm16_mono_wav(struct.pack("<h", 900) * 24, 24000)
    assert wav[:4] == b"RIFF" and wav[8:12] == b"WAVE"
    assert struct.unpack_from("<I", wav, 4)[0] == len(wav) - 8
    assert struct.unpack_from("<HHI", wav, 20) == (1, 1, 24000)
    assert wav[36:40] == b"data" and wav[44:] == struct.pack("<h", 900) * 24
    for invalid, rate in ((b"\0", 24000), (b"\0\0", 0),
                          (b"", 24000), (b"\0\0", 192000)):
        try:
            pcm16_mono_wav(invalid, rate)
            raise AssertionError("Malformed in-memory speech was playable")
        except ValueError:
            pass
    env = startup_environment()
    assert env["SPEECH_NO_UPDATE_CHECK"] == "1"
    assert env["HF_HUB_OFFLINE"] == "1"
    from private_worker_env import self_test as verify_private_env
    verify_private_env()
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
    parser.add_argument("--listen", action="store_true",
                        help="With --run on Windows, synchronously play verified PCM in RAM")
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
    if args.listen and not args.run:
        parser.error("--listen requires explicit --run")
    if (not 1 <= args.samples <= 8 or not 30 <= args.startup_seconds <= 600 or
            not re.fullmatch(r"[A-Za-z0-9_-]{1,80}", args.voice)):
        parser.error("Invalid sample count, startup timeout or voice identifier")
    if args.report is not None and args.report.exists():
        parser.error("Refusing to overwrite an existing acceptance report")
    root = args.root.resolve(strict=True)
    verify(root, root / "offline-assets.json", approved=True, require_voice=True)
    if not (root / "Ember.exe").is_file():
        raise ValueError("Missing native Ember.exe in the assembled payload")
    model_identity(model_path(root, args.model), args.model,
                   selected_voice=args.voice)
    print("Approved offline manifest, actual Qwen3 GGUF task/size and "
          "selected English voice metadata: PASS")
    if not args.run:
        print("PREFLIGHT ONLY. No models, microphone or executable launched.")
        return
    if os.name != "nt":
        parser.error("--run requires native Windows and Windows netstat PID checks")
    report = probe(root, args.model, args.device, args.voice,
                   args.samples, args.startup_seconds, listen=args.listen)
    output = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.report is not None:
        # Exclusive file creation means a later attempt cannot erase a baseline.
        with args.report.open("x", encoding="utf-8") as stream:
            stream.write(output)
    print(output)
    print("Live PCM verified; acoustic quality and clean-PC acceptance remain OPEN.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired,
            HTTPError, URLError, json.JSONDecodeError) as exc:
        sys.exit("Native speech acceptance failed: " +
                 type(exc).__name__ + ": " + str(exc))
