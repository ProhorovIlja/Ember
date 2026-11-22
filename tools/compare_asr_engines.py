#!/usr/bin/env python3
"""Compare user-started local Qwen3-ASR and whisper.cpp on the SAME private EN/RU corpus.

Numerical reports contain no transcript, sample name, file path, WAV or prompt.
Input must be user-owned 16-kHz mono PCM16 WAV referenced by a validated manifest.
Neither model is downloaded, and the Qwen endpoint is fixed to loopback.
"""
import argparse
from functools import partial
import json
import subprocess
import tempfile
import time
from pathlib import Path

from asr_benchmark import MODELS, transcribe
from asr_corpus import evaluate
from localhost_owner import require_owned_speech_listener
from private_worker_env import private_worker_environment


def whisper_adapter(executable, model, vad=None, *, runner=subprocess.run,
                    timeout_seconds=200):
    """Build an explicit private, disposable Whisper CLI adapter.

    No shell is started; Whisper reads only a temporary segment. Stderr/stdout
    are suppressed so transient private words and local paths cannot hit CI.
    """
    executable = Path(executable).expanduser().resolve()
    model = Path(model).expanduser().resolve()
    vad = Path(vad).expanduser().resolve() if vad is not None else None
    if not executable.is_file() or not model.is_file() or (vad and not vad.is_file()):
        raise ValueError("Required user-provided local Whisper executable/model is missing")
    if not 1 <= model.stat().st_size or (vad and vad.stat().st_size < 1):
        raise ValueError("Whisper model asset is empty")
    if not 15 <= timeout_seconds <= 900:
        raise ValueError("Whisper timeout must be between 15 and 900 seconds")

    def recognize(wav_data, _model, language, *, omit_model_id=False):
        if language not in ("auto", "en", "ru"):
            raise ValueError("Unsupported language for Whisper")
        if len(wav_data) < 44 or len(wav_data) > 25 * 1024 * 1024:
            raise ValueError("Oversized or invalid private WAV segment")
        with tempfile.TemporaryDirectory(prefix="ember-private-asr-") as work:
            recording = Path(work) / "microphone.wav"
            output = Path(work) / "recognition"
            recording.write_bytes(wav_data)
            argv = [str(executable), "-m", str(model), "-f", str(recording),
                    "-l", language, "-otxt", "-of", str(output), "-np"]
            if vad is not None:
                argv += ["--vad", "-vm", str(vad)]
            started = time.perf_counter()
            try:
                done = runner(argv, cwd=str(executable.parent),
                              stdin=subprocess.DEVNULL,
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL,
                              timeout=timeout_seconds, check=False,
                              env=private_worker_environment())
            except subprocess.TimeoutExpired:
                raise RuntimeError("Local Whisper transcription timed out") from None
            except OSError:
                raise RuntimeError("Local Whisper executable could not run") from None
            elapsed = round((time.perf_counter() - started) * 1000, 2)
            if done.returncode != 0:
                raise RuntimeError("Local Whisper exited unsuccessfully")
            result = output.with_suffix(".txt")
            if result.is_symlink() or not result.is_file() or result.stat().st_size > 65536:
                raise RuntimeError("Local Whisper omitted or oversized its text response")
            transcript = result.read_text(encoding="utf-8-sig").strip()
            if len(transcript) > 4000:
                raise RuntimeError("Local Whisper transcription exceeded report bounds")
            return transcript, elapsed

    return recognize


def comparison_report(manifest, qwen_model, whisper_recognize, *,
                      omit_qwen_model=False, qwen_recognize=transcribe, order="whisper-first"):
    """Run both engines over identical validated/segmented user recordings."""
    if qwen_model not in MODELS:
        raise ValueError("Unsupported Qwen3-ASR model")
    if order not in ("whisper-first", "qwen-first"):
        raise ValueError("Unsupported evaluation order")
    if order == "whisper-first":
        whisper = evaluate(manifest, qwen_model, True, whisper_recognize, True)
        qwen = evaluate(manifest, qwen_model, omit_qwen_model, qwen_recognize, True)
    else:
        qwen = evaluate(manifest, qwen_model, omit_qwen_model, qwen_recognize, True)
        whisper = evaluate(manifest, qwen_model, True, whisper_recognize, True)
    shape = lambda row: (row["trial"], row["language"],
                         row["duration_seconds"], row["segments"])
    if list(map(shape, whisper["trials"])) != list(map(shape, qwen["trials"])):
        raise RuntimeError("The two models received different corpus segments")
    paired_trials = [{
        "trial": a["trial"], "language": a["language"],
        "qwen_minus_whisper_wer": round(b["wer"] - a["wer"], 4),
        "qwen_minus_whisper_cer": round(b["cer"] - a["cer"], 4),
        "qwen_minus_whisper_asr_wall_ms": round(b["asr_wall_ms"] - a["asr_wall_ms"], 2),
    } for a, b in zip(whisper["trials"], qwen["trials"])]
    whisper["model_label"] = "whisper.cpp (user-supplied model)"
    whisper["omitted_catalog_model_id"] = None
    delta = {}
    for lang in ("overall", "en", "ru", "auto"):
        a = whisper["overall"] if lang == "overall" else whisper["by_language"].get(lang)
        b = qwen["overall"] if lang == "overall" else qwen["by_language"].get(lang)
        if a is None or b is None:
            continue
        delta[lang] = {
            "qwen_minus_whisper_wer": round(b["wer"] - a["wer"], 4),
            "qwen_minus_whisper_cer": round(b["cer"] - a["cer"], 4),
            "qwen_minus_whisper_mean_wall_ms": round(b["mean_asr_wall_ms"] - a["mean_asr_wall_ms"], 2),
        }
    return {
        "schema_version": 1,
        "evaluation_order": order,
        "note": ("Paired, user-supplied private audio comparison. Numeric metrics only. "
                 "A negative delta favors Qwen on this sample; not statistical "
                 "proof of universal superiority. Timings are sequential and "
                 "may be affected by GPU/CPU cache and contention."),
        "input_policy": "16kHz mono PCM16; 25s chunks; 500ms overlap; pause-aware seams",
        "engines": {"whisper_cpp": whisper, "qwen3_asr": qwen},
        "paired_differences": delta,
        "paired_trials": paired_trials,
    }


def self_test():
    import wave
    from asr_benchmark import silent_wav
    from private_worker_env import self_test as verify_private_env
    verify_private_env()
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        exe = root / "whisper-cli.exe"  # fake file; runner below is injected
        model = root / "tiny-model.gguf"
        vad = root / "vad.gguf"
        for path in (exe, model, vad):
            path.write_bytes(b"test-only")
        (root / "recordings").mkdir()
        (root / "refs").mkdir()
        (root / "recordings" / "one.wav").write_bytes(silent_wav())
        (root / "recordings" / "two.wav").write_bytes(silent_wav())
        samples = [("one", "en", "A clear hello."),
                   ("two", "ru", "Привет и до свидания.")]
        for stem, lang, phrase in samples:
            (root / "refs" / f"{stem}.txt").write_text(phrase, encoding="utf-8")
        manifest = root / "corpus.json"
        manifest.write_text(json.dumps({"cases": [
            {"audio": f"recordings/{stem}.wav", "reference": f"refs/{stem}.txt",
             "language": lang} for stem, lang, _ in samples
        ]}), encoding="utf-8")
        created = []

        def fake_run(args, *, cwd, stdin, stdout, stderr, timeout, check, env):
            assert args[0] == str(exe.resolve())
            assert args[1:3] == ["-m", str(model.resolve())]
            assert args[3] == "-f" and args[5] == "-l"
            assert args[-3:] == ["--vad", "-vm", str(vad.resolve())]
            assert stdin == stdout == stderr == subprocess.DEVNULL
            assert not check and timeout == 200
            assert env["SPEECH_NO_UPDATE_CHECK"] == "1"
            assert env["HF_HUB_OFFLINE"] == "1"
            assert not any("TOKEN" in key.upper() or "API_KEY" in key.upper()
                           for key in env)
            src = Path(args[4])
            assert src.exists()
            with wave.open(str(src), "rb") as reader:
                assert reader.getframerate() == 16000 and reader.getnchannels() == 1
            created.append(src)
            lang = args[6]
            Path(args[args.index("-of") + 1] + ".txt").write_text(
                "A clear hello." if lang == "en" else "Привет и до свидания.",
                encoding="utf-8")
            return subprocess.CompletedProcess(args, 0)

        whisper = whisper_adapter(exe, model, vad, runner=fake_run)

        def fake_qwen(_wav, model_name, lang, *, omit_model_id=False):
            assert omit_model_id and model_name == MODELS[0]
            return ("A clear hello." if lang == "en" else "Привет и до свидания."), 15.0

        report = comparison_report(manifest, MODELS[0], whisper,
                                   omit_qwen_model=True, qwen_recognize=fake_qwen)
        assert report["engines"]["whisper_cpp"]["overall"]["wer"] == 0
        assert report["engines"]["qwen3_asr"]["overall"]["cer"] == 0
        assert len(created) == 2 and all(not p.exists() for p in created)
        assert set(report["paired_differences"]) == {"overall", "en", "ru"}
        assert len(report["paired_trials"]) == 2
        assert all(row["qwen_minus_whisper_wer"] == 0 for row in report["paired_trials"])
        raw = json.dumps(report, ensure_ascii=False)
        for private_value in ("clear hello", "Привет", "recordings", str(root)):
            assert private_value.casefold() not in raw.casefold()
        try:
            whisper_adapter(exe, root / "missing.gguf")
            raise AssertionError("Missing model was accepted")
        except ValueError:
            pass

        def bad_run(args, **_kwargs):
            return subprocess.CompletedProcess(args, 1)

        try:
            whisper_adapter(exe, model, runner=bad_run)(silent_wav(), MODELS[0], "en")
            raise AssertionError("Whisper failure was silently accepted")
        except RuntimeError:
            pass
        reversed_report = comparison_report(manifest, MODELS[0], whisper,
                                            omit_qwen_model=True,
                                            qwen_recognize=fake_qwen, order="qwen-first")
        assert reversed_report["evaluation_order"] == "qwen-first"
        assert reversed_report["paired_differences"]["overall"]["qwen_minus_whisper_wer"] == 0
        assert len(created) == 4 and all(not p.exists() for p in created)
    print("paired local Whisper/Qwen EN/RU ASR safety and metrics self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--qwen-model", choices=MODELS, default=MODELS[0])
    parser.add_argument("--speech-pid", type=int,
                        help="Required trusted Windows speech.exe PID at 127.0.0.1:43822")
    parser.add_argument("--omit-qwen-model-id", action="store_true")
    parser.add_argument("--order", choices=("whisper-first", "qwen-first"),
                        default="whisper-first", help="Alternate to identify cache/order bias")
    parser.add_argument("--whisper-exe", type=Path)
    parser.add_argument("--whisper-model", type=Path)
    parser.add_argument("--whisper-vad-model", type=Path)
    parser.add_argument("--output", type=Path, help="New JSON report; numeric data only")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not all((args.manifest, args.whisper_exe, args.whisper_model)):
        parser.error("--manifest, --whisper-exe and --whisper-model are required")
    if not args.speech_pid or args.speech_pid <= 0:
        parser.error("--speech-pid is required before reading private ASR recordings")
    try:
        require_owned_speech_listener(args.speech_pid)
        recognizer = whisper_adapter(args.whisper_exe, args.whisper_model,
                                     args.whisper_vad_model)
        result = comparison_report(args.manifest, args.qwen_model, recognizer,
                                   omit_qwen_model=args.omit_qwen_model_id,
                                   qwen_recognize=partial(transcribe, expected_pid=args.speech_pid),
                                   order=args.order)
        output = json.dumps(result, ensure_ascii=False, indent=2) + "\n"
        if args.output:
            if args.output.exists():
                parser.error("Refusing to overwrite an existing private metrics report")
            args.output.write_text(output, encoding="utf-8")
        print(output, end="")
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError, RuntimeError,
            subprocess.SubprocessError):
        parser.error("Private ASR comparison failed; inspect local engines, "
                     "source format, and model settings. No private source data logged.")


if __name__ == "__main__":
    main()
