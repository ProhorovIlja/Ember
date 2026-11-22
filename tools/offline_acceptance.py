#!/usr/bin/env python3
"""Manual, blocking Windows acceptance of an already assembled offline Ember payload.

The default action is READ-ONLY PREFLIGHT. Only --run starts user-supplied
executables. No downloads, registry edits, scheduled tasks or background
monitoring. Python stdlib and sibling Ember tooling only. It is NOT a
substitute for human release, license and network-isolation review.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import wave
from urllib.error import HTTPError, URLError
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener

from offline_bundle import verify
from asr_benchmark import metrics
from text_benchmark import ENDPOINT, measure

PORT = 43821
HEALTH = "http://127.0.0.1:43821/health"
MAX_WAV = 25 * 1024 * 1024


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


NO_PROXY = build_opener(ProxyHandler({}), NoRedirect())


def netstat_listening_pids(output):
    """Read only IPv4 loopback LISTEN socket owners; ignore connected sockets.

    Windows' netstat state text is localized; the zero remote endpoint is
    independent of whether LISTENING is translated.
    """
    found = set()
    for line in output.splitlines():
        parts = line.split()
        if (len(parts) >= 5 and parts[0].upper() == "TCP" and
                parts[1] == f"127.0.0.1:{PORT}" and
                parts[2] == "0.0.0.0:0" and parts[-1].isdigit()):
            found.add(int(parts[-1]))
    return found


def listener_owners():
    done = subprocess.run(["netstat", "-ano", "-p", "tcp"],
                          stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL, check=False, timeout=10)
    if done.returncode:
        raise RuntimeError("Cannot inspect Windows listening socket ownership")
    return netstat_listening_pids(done.stdout.decode("ascii", "ignore"))


def wait_for_server(process, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError("The native llama-server stopped during model loading")
        owners = listener_owners()
        if owners and owners != {process.pid}:
            raise RuntimeError("Port 43821 is owned by another process: refusing HTTP")
        if owners == {process.pid}:
            try:
                with NO_PROXY.open(Request(HEALTH, method="GET"), timeout=3) as result:
                    if result.status == 200:
                        body = result.read(4097)
                        if len(body) > 4096:
                            raise RuntimeError("Local health response is oversized")
                        state = json.loads(body)
                        if state.get("status") == "ok":
                            return
            except (HTTPError, URLError, TimeoutError):
                pass  # 503 while the GGUF is still being loaded
        time.sleep(1)
    raise RuntimeError("Local model was not ready before the bounded timeout")


def restricted_environment():
    env = os.environ.copy()
    for key in list(env):
        upper = key.upper()
        if (any(marker in upper for marker in
                ("TOKEN", "SECRET", "PASSWORD", "CREDENTIAL", "API_KEY")) or
                upper.startswith(("AWS_", "GITHUB_", "OPENAI_", "GOOGLE_", "AZURE_"))):
            env.pop(key, None)
    return env


def smoke_text(root, context, gpu_layers, seconds):
    if listener_owners():
        raise RuntimeError("Local text port 43821 is already in use; stop that server first")
    exe = root / "engines/llama/llama-server.exe"
    weights = root / "models/llm/model.gguf"
    args = [str(exe), "--model", str(weights), "--alias", "ember",
            "--host", "127.0.0.1", "--port", str(PORT),
            "--ctx-size", str(context), "--n-gpu-layers", str(gpu_layers),
            "--parallel", "1", "--jinja"]
    proc = subprocess.Popen(args, cwd=str(exe.parent),
                            stdin=subprocess.DEVNULL,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                            env=restricted_environment())
    try:
        wait_for_server(proc, seconds)
        if listener_owners() != {proc.pid}:
            raise RuntimeError("Text endpoint lost its owner identity")
        # This is the same streaming /v1/chat/completions transport Ember uses.
        # The sibling probe rejects absent [DONE], redirects, malformed SSE.
        result = measure(ENDPOINT, "instruction",
                         "Reply with exactly the word READY.")
        if listener_owners() != {proc.pid} or not result["instruction_exact_match"]:
            raise RuntimeError("Model's streamed instruction-following test failed")
        return {"stream_complete": True,
                "exact_instruction": True,
                "time_to_first_token_ms": result["ttft_ms"],
                "total_response_ms": result["full_response_ms"]}
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=8)


def validate_local_wav(path):
    if path.is_symlink() or not path.is_file():
        raise ValueError("The selected WAV must be a regular local file")
    size = path.stat().st_size
    if not 44 <= size <= MAX_WAV:
        raise ValueError("WAV is missing or exceeds the 25 MiB limit")
    with wave.open(str(path), "rb") as source:
        if (source.getcomptype() != "NONE" or source.getnchannels() != 1 or
                source.getsampwidth() != 2 or source.getframerate() != 16000 or
                source.getnframes() <= 0):
            raise ValueError("Expected 16-kHz mono PCM16 WAV")
        if source.getnframes() * 2 > size:
            raise ValueError("WAV declares more frames than file can contain")
    return size


def smoke_whisper(root, wav_path, ref_path, lang, max_wer):
    validate_local_wav(wav_path)
    if ref_path.is_symlink() or not ref_path.is_file() or ref_path.stat().st_size > 16000:
        raise ValueError("Expected a local UTF-8 reference transcript <=16 KiB")
    reference = ref_path.read_text(encoding="utf-8").strip()
    if not 1 <= len(reference) <= 4000:
        raise ValueError("Reference transcript must have 1-4000 characters")
    executable = root / "engines/whisper/whisper-cli.exe"
    weights = root / "models/stt/ggml-base.bin"
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix="ember-once-asr-") as tmp:
        prefix = Path(tmp) / "recognized"
        args = [str(executable), "-m", str(weights), "-f", str(wav_path),
                "-l", lang, "-otxt", "-of", str(prefix), "-np"]
        completed = subprocess.run(args, cwd=str(executable.parent),
                                   env=restricted_environment(),
                                   stdin=subprocess.DEVNULL,
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                   timeout=240, check=False)
        if completed.returncode:
            raise RuntimeError(f"Native Whisper {lang} process failed")
        output = prefix.with_suffix(".txt")
        if not output.is_file() or output.stat().st_size > 16000:
            raise RuntimeError(f"Native Whisper {lang} produced no bounded transcript")
        prediction = output.read_text(encoding="utf-8").strip()
    if not prediction:
        raise RuntimeError(f"Native Whisper {lang} recognized no speech")
    score = metrics(reference, prediction)
    if score["wer"] > max_wer:
        raise RuntimeError(f"Native Whisper {lang} WER exceeds selected limit")
    return {"language": lang, "wer": score["wer"], "cer": score["cer"],
            "latency_ms": round((time.monotonic() - started) * 1000, 2),
            "wav_bytes": wav_path.stat().st_size}


def self_test():
    fixture = """Active Connections
      Proto  Local Address          Foreign Address        State           PID
      TCP    127.0.0.1:43821       0.0.0.0:0             LISTENING       1234
      TCP    127.0.0.1:43821       127.0.0.1:6000        ESTABLISHED    1234
      TCP    0.0.0.0:43821         0.0.0.0:0             LISTENING       9999
      TCP    127.0.0.1:43822       0.0.0.0:0             LISTENING       8888
    """
    assert netstat_listening_pids(fixture) == {1234}
    assert netstat_listening_pids("") == set()
    assert netstat_listening_pids(fixture.replace("1234", "9999")) == {9999}
    from asr_benchmark import silent_wav
    with tempfile.TemporaryDirectory() as tmp:
        sample = Path(tmp) / "silent.wav"
        sample.write_bytes(silent_wav())
        assert validate_local_wav(sample) == sample.stat().st_size
        sample.write_bytes(b"RIFF" + bytes(200))
        try:
            validate_local_wav(sample)
            raise AssertionError("Malformed WAV was accepted")
        except (ValueError, wave.Error, EOFError):
            pass
    print("offline_acceptance read-only fixture tests passed; no inference executed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, help="Fully assembled offline Ember app folder")
    parser.add_argument("--run", action="store_true",
                        help="Explicitly execute native models now; blocks until complete")
    parser.add_argument("--context", type=int, choices=(4096, 8192, 16384, 32768),
                        default=8192)
    parser.add_argument("--gpu-layers", type=int, choices=(0, 20, 50, 99), default=0)
    parser.add_argument("--startup-seconds", type=int, default=180)
    for lang in ("en", "ru"):
        parser.add_argument(f"--{lang}-wav", type=Path)
        parser.add_argument(f"--{lang}-reference", type=Path)
    parser.add_argument("--max-wer", type=float, default=0.25)
    parser.add_argument("--report", type=Path,
                        help="Optional metrics-only JSON; never transcripts or audio")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.root is None:
        parser.error("--root is required except during --self-test")
    if not 30 <= args.startup_seconds <= 600 or not 0 <= args.max_wer <= 1:
        parser.error("Invalid startup time or WER threshold")
    root = args.root.resolve(strict=True)
    verify(root, root / "offline-assets.json", approved=True)
    for relative in ("Ember.exe", "Qt6Core.dll", "LICENSE"):
        if not (root / relative).is_file():
            raise ValueError("Missing native application payload: " + relative)
    print("Approved manifest, required paths, header shapes and SHA-256: PASS")
    if not args.run:
        print("PREFLIGHT ONLY: no executable, model or microphone was started.")
        return
    if os.name != "nt":
        parser.error("Live native executable acceptance requires Windows")
    audio_inputs = [args.en_wav, args.en_reference, args.ru_wav, args.ru_reference]
    if any(x is not None for x in audio_inputs) and not all(
            x is not None for x in audio_inputs):
        parser.error("Provide all four EN/RU WAV + reference files, or none")
    result = {"native_text": smoke_text(root, args.context,
                                        args.gpu_layers, args.startup_seconds),
              "native_ru_en_dictation": "NOT TESTED",
              "two_sample_asr_checked": False,
              "release_approved": False,
              "network_note": ("The Python HTTP probe contacts 127.0.0.1 only; "
                               "network isolation of third-party binaries was "
                               "not independently proven.")}
    if all(x is not None for x in audio_inputs):
        result["native_ru_en_dictation"] = [
            smoke_whisper(root, args.en_wav, args.en_reference, "en", args.max_wer),
            smoke_whisper(root, args.ru_wav, args.ru_reference, "ru", args.max_wer),
        ]
        result["two_sample_asr_checked"] = True
    content = json.dumps(result, ensure_ascii=False, indent=2)
    if args.report:
        if args.report.exists():
            raise ValueError("Refusing to overwrite an existing report")
        args.report.write_text(content + "\n", encoding="utf-8")
    print(content)
    print("Manual test finished. Human license, clean-PC, GPU, privacy and UI gates remain.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired,
            HTTPError, URLError, json.JSONDecodeError, wave.Error) as exc:
        sys.exit("Offline acceptance failed: " + type(exc).__name__ + ": " + str(exc))
