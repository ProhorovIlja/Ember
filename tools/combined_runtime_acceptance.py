#!/usr/bin/env python3
"""Manual one-shot Windows test: keep Ember LLM + native English speech loaded together.

An offline installer that tests text and TTS separately may still fail on a
12-GiB RTX 4070 when both engines allocate VRAM. This foreground acceptance
starts TWO approved native child processes, proves both listeners and sends
one real text and one real PCM request *while both remain resident*, then
terminates both in a finally block. No downloads, monitoring, schedules,
conversation transcripts or audio files. Not a human voice-quality judgment.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from urllib.error import HTTPError, URLError

from offline_bundle import verify, verify_windows_amd64_pe
from offline_acceptance import (listener_owners, wait_for_server,
                                verify_real_prompt_tokens)
from native_speech_acceptance import (model_path, speech_owners, wait_for_speech)
from private_worker_env import private_worker_environment
from brain_mode_acceptance import CASES as BRAIN_CASES, run_case as brain_case
from voice_benchmark import (ENDPOINT as SPEECH_ENDPOINT, PROMPTS,
                             measure as speech_measure)


def validated_args(context: int, gpu_layers: int, device: str, samples: int,
                   timeout: int, voice: str, model: str) -> None:
    if context not in (4096, 8192, 16384, 32768):
        raise ValueError("Unsupported context size")
    if gpu_layers not in (0, 20, 50, 99):
        raise ValueError("Unsupported GPU layer count")
    if device not in ("auto", "cpu", "gpu"):
        raise ValueError("Unsupported native speech device")
    if type(samples) is not int or not 1 <= samples <= 4:
        raise ValueError("Expected one to four test speech samples")
    if type(timeout) is not int or not 30 <= timeout <= 600:
        raise ValueError("Invalid model startup timeout")
    if voice not in ("vivian", "serena"):
        raise ValueError("Only listed English female speaker IDs are accepted")
    if model not in ("qwen3-tts-0.6b", "qwen3-tts-1.7b"):
        raise ValueError("Unsupported native voice model alias")


def workers_alive(brain, speech) -> None:
    if brain.poll() is not None or speech.poll() is not None:
        raise RuntimeError("Ember LLM or TTS exited while both were required")
    if listener_owners() != {brain.pid} or speech_owners() != {speech.pid}:
        raise RuntimeError("A localhost model listener changed ownership")


def stop_worker(proc) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.terminate()
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=8)


def probe(root: Path, *, context: int, gpu_layers: int, device: str,
          samples: int, startup_seconds: int, voice: str, model: str) -> dict:
    validated_args(context, gpu_layers, device, samples,
                   startup_seconds, voice, model)
    if listener_owners() or speech_owners():
        raise RuntimeError("Ember ports are already occupied: stop local engines first")
    verify(root, root / "offline-assets.json", approved=True, require_voice=True)
    brain_exe = root / "engines/llama/llama-server.exe"
    speech_exe = root / "engines/speech/speech.exe"
    verify_windows_amd64_pe(brain_exe, "engines/llama/llama-server.exe")
    verify_windows_amd64_pe(speech_exe, "engines/speech/speech.exe")
    llm = root / "models/llm/model.gguf"
    tts = model_path(root, model)
    # model_path verifies GGUF family and size; wait_for_speech later checks
    # the *actually loaded* inventory and chosen English female speaker.
    brain_argv = [str(brain_exe), "--model", str(llm), "--alias", "ember",
                  "--host", "127.0.0.1", "--port", "43821", "--ctx-size",
                  str(context), "--n-gpu-layers", str(gpu_layers),
                  "--parallel", "1", "--jinja"]
    speech_argv = [str(speech_exe), "serve", str(tts), "--host", "127.0.0.1",
                   "--port", "43822", "--device", device, "--threads", "4"]
    llm_proc = None
    voice_proc = None
    try:
        llm_proc = subprocess.Popen(
            brain_argv, cwd=str(brain_exe.parent), env=private_worker_environment(),
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
        wait_for_server(llm_proc, startup_seconds)
        voice_proc = subprocess.Popen(
            speech_argv, cwd=str(speech_exe.parent), env=private_worker_environment(),
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL)
        # This is the real memory-contention gate: llama.cpp remains loaded
        # throughout native speech.cpp startup, synthesis AND another text turn.
        wait_for_speech(voice_proc, startup_seconds, model, voice)
        workers_alive(llm_proc, voice_proc)
        metrics = []
        first_text = brain_case(BRAIN_CASES[0], llm_proc.pid)
        workers_alive(llm_proc, voice_proc)
        if not first_text.get("passed"):
            raise RuntimeError("Ember text response failed with both engines resident")
        for i in range(samples):
            speech = speech_measure(
                SPEECH_ENDPOINT, model, voice, PROMPTS[i % len(PROMPTS)],
                "natural", omit_model_id=True, seed=i + 1,
                expected_pid=voice_proc.pid)
            workers_alive(llm_proc, voice_proc)
            if (not speech.get("digital_voice_detected") or
                    speech.get("audio_seconds", 0) < 0.2):
                raise RuntimeError("TTS returned silence or incomplete PCM under co-residency")
            metrics.append({"trial": i + 1, "audio_seconds": speech["audio_seconds"],
                            "first_pcm_byte_ms": speech["first_http_pcm_byte_ms"],
                            "total_synthesis_ms": speech["total_http_synthesis_ms"]})
        # A second, Russian nonthinking exchange after TTS checks that
        # the local model still obeys the official Qwen3.5 template under load.
        second_text = brain_case(BRAIN_CASES[1], llm_proc.pid)
        workers_alive(llm_proc, voice_proc)
        if not second_text.get("passed"):
            raise RuntimeError("Text model failed after local voice synthesis")
        tokenizer = verify_real_prompt_tokens(llm_proc.pid, context)
        workers_alive(llm_proc, voice_proc)
        return {
            "simultaneous_llm_tts_residency": "PASS",
            "text_round_trips": 2,
            "speech_samples": len(metrics),
            "first_english_visible_ms": first_text["first_visible_ms"],
            "second_russian_visible_ms": second_text["first_visible_ms"],
            "speech_metrics_only": metrics,
            "loaded_tts_alias": model,
            "requested_speech_device": device,
            "requested_gpu_layers": gpu_layers,
            "token_budget": tokenizer,
            "actual_peak_vram_bytes": None,
            "release_approved": False,
            "caveat": "Co-residency and PCM responses do not prove natural voice, "
                      "correct GPU offload, long dialogues or full-duplex barge-in.",
        }
    finally:
        # Kill only our own explicitly spawned children; never interfere with
        # pre-existing Ember instances or other applications.
        stop_worker(voice_proc)
        stop_worker(llm_proc)


def self_test() -> None:
    """No Windows processes or TCP requests. Exercise both-worker rollback."""
    for bad in [(2048, 99, "auto", 1, 180, "vivian", "qwen3-tts-1.7b"),
                (8192, 100, "auto", 1, 180, "vivian", "qwen3-tts-1.7b"),
                (8192, 99, "remote", 1, 180, "vivian", "qwen3-tts-1.7b"),
                (8192, 99, "auto", 0, 180, "vivian", "qwen3-tts-1.7b"),
                (8192, 99, "auto", 1, 2, "vivian", "qwen3-tts-1.7b"),
                (8192, 99, "auto", 1, 180, "unknown", "qwen3-tts-1.7b"),
                (8192, 99, "auto", 1, 180, "vivian", "qwen3-tts-unknown")]:
        try:
            validated_args(*bad)
            raise AssertionError("Unsafe runtime parameters were accepted")
        except ValueError:
            pass
    validated_args(8192, 99, "auto", 2, 180, "vivian", "qwen3-tts-1.7b")

    import unittest.mock as mock
    class Child:
        def __init__(self, pid):
            self.pid, self.stopped = pid, False
        def poll(self):
            return 0 if self.stopped else None
        def terminate(self):
            self.stopped = True
        def kill(self):
            self.stopped = True
        def wait(self, timeout):
            return 0

    def fake_text(*args):
        return {"passed": True, "first_visible_ms": 12.0}
    def fake_speech(*args, **kwargs):
        return {"digital_voice_detected": True, "audio_seconds": 0.4,
                "first_http_pcm_byte_ms": 14.0, "total_http_synthesis_ms": 80.0}

    with tempfile.TemporaryDirectory(prefix="ember-both-workers-test-") as tmp:
        root = Path(tmp)
        children = []
        def fake_popen(*args, **kwargs):
            proc = Child(100 + len(children))
            children.append(proc)
            return proc
        with (mock.patch(__name__ + ".listener_owners", side_effect=lambda: {100} if children and not children[0].stopped else set()),
              mock.patch(__name__ + ".speech_owners", side_effect=lambda: {101} if len(children) > 1 and not children[1].stopped else set()),
              mock.patch(__name__ + ".verify"),
              mock.patch(__name__ + ".verify_windows_amd64_pe"),
              mock.patch(__name__ + ".model_path", return_value=root / "voice.gguf"),
              mock.patch(__name__ + ".wait_for_server"),
              mock.patch(__name__ + ".wait_for_speech"),
              mock.patch(__name__ + ".verify_real_prompt_tokens", return_value={"configured_context_tokens": 8192}),
              mock.patch(__name__ + ".brain_case", side_effect=fake_text),
              mock.patch(__name__ + ".speech_measure", side_effect=fake_speech),
              mock.patch(__name__ + ".subprocess.Popen", side_effect=fake_popen)):
            result = probe(root, context=8192, gpu_layers=99, device="auto",
                           samples=2, startup_seconds=180, voice="vivian",
                           model="qwen3-tts-1.7b")
            assert result["simultaneous_llm_tts_residency"] == "PASS"
            assert len(children) == 2 and all(c.stopped for c in children)
            children.clear()
            with mock.patch(__name__ + ".wait_for_speech", side_effect=RuntimeError("simulated OOM")):
                try:
                    probe(root, context=8192, gpu_layers=99, device="auto",
                          samples=1, startup_seconds=180, voice="vivian",
                          model="qwen3-tts-1.7b")
                    raise AssertionError("Simulated speech startup OOM was silently accepted")
                except RuntimeError as error:
                    assert "simulated OOM" in str(error)
            assert len(children) == 2 and all(c.stopped for c in children)
    print("combined_runtime_acceptance: validation, dual residency and OOM rollback fixtures PASS")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--model", choices=("qwen3-tts-0.6b", "qwen3-tts-1.7b"),
                        default="qwen3-tts-1.7b")
    parser.add_argument("--voice", choices=("vivian", "serena"), default="vivian")
    parser.add_argument("--device", choices=("auto", "cpu", "gpu"), default="auto")
    parser.add_argument("--context", type=int, default=8192)
    parser.add_argument("--gpu-layers", type=int, default=99)
    parser.add_argument("--samples", type=int, default=2)
    parser.add_argument("--startup-seconds", type=int, default=180)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.run or args.root is None:
        parser.error("Live co-residency requires explicit --run and --root")
    if os.name != "nt":
        parser.error("Live native LLM/TTS co-residency test requires Windows")
    if args.report is not None and args.report.exists():
        parser.error("Refusing to overwrite an existing metrics report")
    try:
        result = probe(args.root.resolve(strict=True), context=args.context,
                       gpu_layers=args.gpu_layers, device=args.device,
                       samples=args.samples, startup_seconds=args.startup_seconds,
                       voice=args.voice, model=args.model)
        encoded = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
        if args.report is not None:
            with args.report.open("x", encoding="utf-8") as out:
                out.write(encoded)
        print(encoded)
        print("Both model engines loaded and responded; human voice/VRAM QA is NOT complete.")
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired,
            HTTPError, URLError, json.JSONDecodeError) as exc:
        sys.exit("Combined Ember runtime acceptance failed: " +
                 type(exc).__name__ + ": " + str(exc))


if __name__ == "__main__":
    main()
