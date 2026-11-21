#!/usr/bin/env python3
"""Local EN/RU ASR corpus validation without writing audio or transcripts.

The user supplies a JSON manifest pointing ONLY to existing local WAV and
reference TXT files. Requests go only to 127.0.0.1:43822; reports contain
numeric error/latency metrics, never the referenced words or filenames.
"""
import argparse
import io
import json
import statistics
import tempfile
import wave
from pathlib import Path, PurePosixPath

from asr_benchmark import MAX_WAV, MODELS, metrics, silent_wav, transcribe


def bounded_local_file(root, relative, maximum):
    if (not isinstance(relative, str) or not 1 <= len(relative) <= 220 or
            "\\" in relative or ":" in relative or "//" in relative):
        raise ValueError("Manifest assets must be bounded relative POSIX paths")
    parts = PurePosixPath(relative)
    if parts.is_absolute() or any(part in (".", "..") for part in parts.parts):
        raise ValueError("Absolute or parent traversal paths are forbidden")
    path = root
    for part in parts.parts:
        path = path / part
        if path.is_symlink():
            raise ValueError("Symlinked manifest files/directories are forbidden")
    if not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("Manifest asset must be a regular file inside its folder")
    size = path.stat().st_size
    if not 0 < size <= maximum:
        raise ValueError("Manifest asset violates per-file size limit")
    return path


def check_wave(data):
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE" or len(data) > MAX_WAV:
        raise ValueError("Only bounded uncompressed PCM RIFF WAV is supported")
    try:
        with wave.open(io.BytesIO(data), "rb") as reader:
            rate = reader.getframerate()
            frames = reader.getnframes()
            if (reader.getcomptype() != "NONE" or
                    reader.getsampwidth() != 2 or
                    reader.getnchannels() not in (1, 2) or
                    not 8000 <= rate <= 96000 or frames <= 0):
                raise ValueError("WAV must contain 16-bit 1/2-channel PCM")
            return round(frames / rate, 3)
    except (EOFError, wave.Error) as exc:
        raise ValueError("Corrupt or unsupported RIFF WAV") from exc


def evaluate(manifest, model, omit_model_id=False, recognizer=transcribe):
    if model not in MODELS:
        raise ValueError("Unsupported local Qwen ASR model label")
    manifest = Path(manifest)
    root = manifest.resolve().parent
    if manifest.is_symlink() or manifest.stat().st_size > 128 * 1024:
        raise ValueError("Unsafe or oversized corpus manifest")
    structure = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(structure, dict):
        raise ValueError("Manifest must be an object with a cases array")
    cases = structure.get("cases")
    if not isinstance(cases, list) or not 1 <= len(cases) <= 100:
        raise ValueError("Corpus must have 1 to 100 cases")
    seen = set()
    rows = []
    for index, item in enumerate(cases):
        if not isinstance(item, dict) or set(item) != {"audio", "reference", "language"}:
            raise ValueError("Every case requires only audio, reference and language")
        language = item["language"]
        if language not in ("en", "ru", "auto"):
            raise ValueError("Only English, Russian and auto languages are supported")
        audio_path = bounded_local_file(root, item["audio"], MAX_WAV)
        ref_path = bounded_local_file(root, item["reference"], 16 * 1024)
        identity = audio_path.resolve()
        if identity in seen:
            raise ValueError("Duplicate recordings bias corpus results")
        seen.add(identity)
        wav = audio_path.read_bytes()
        duration = check_wave(wav)
        expected = ref_path.read_text(encoding="utf-8").strip()
        if not 0 < len(expected) <= 4000:
            raise ValueError("Reference transcript must contain up to 4000 characters")
        prediction, elapsed_ms = recognizer(wav, model, language,
                                            omit_model_id=omit_model_id)
        trial = metrics(expected, prediction)
        rows.append({
            "trial": index + 1, "language": language,
            "duration_seconds": duration, "asr_wall_ms": elapsed_ms,
            "wer": trial["wer"], "cer": trial["cer"],
            "reference_words": trial["reference_word_count"],
            "reference_chars": trial["reference_character_count"],
            "word_edits": trial["word_edit_distance"],
            "character_edits": trial["character_edit_distance"],
        })
    def summarize(entries):
        words = sum(x["reference_words"] for x in entries)
        characters = sum(x["reference_chars"] for x in entries)
        durations = sum(x["duration_seconds"] for x in entries)
        elapsed = sum(x["asr_wall_ms"] for x in entries)
        return {
            "cases": len(entries),
            "wer": round(sum(x["word_edits"] for x in entries) / max(1, words), 4),
            "cer": round(sum(x["character_edits"] for x in entries) / max(1, characters), 4),
            "real_time_factor": round((elapsed / 1000) / max(0.001, durations), 3),
            "mean_asr_wall_ms": round(statistics.mean(
                x["asr_wall_ms"] for x in entries), 2),
            "audio_seconds": round(durations, 3),
        }
    return {
        "note": ("This is a user-supplied local benchmark, not official WER. "
                 "A single model/ASR can err on proper nouns. No text/audio or "
                 "file paths are saved in the report."),
        "model_label": model,
        "omitted_catalog_model_id": omit_model_id,
        "overall": summarize(rows),
        "by_language": {lang: summarize([r for r in rows if r["language"] == lang])
                        for lang in ("en", "ru", "auto")
                        if any(r["language"] == lang for r in rows)},
        "trials": rows,
    }


def self_test():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        (root / "recordings").mkdir()
        (root / "refs").mkdir()
        for i, (label, language) in enumerate(
                (("Hello world.", "en"), ("Привет мир.", "ru")), 1):
            (root / "recordings" / f"{i}.wav").write_bytes(silent_wav())
            (root / "refs" / f"{i}.txt").write_text(label, encoding="utf-8")
        manifest = root / "corpus.json"
        items = [
            {"audio": f"recordings/{i}.wav",
             "reference": f"refs/{i}.txt", "language": lang}
            for i, lang in ((1, "en"), (2, "ru"))
        ]
        manifest.write_text(json.dumps({"cases": items}), encoding="utf-8")
        def recognize(_data, _model, language, omit_model_id=False):
            assert omit_model_id
            return ("Hello world." if language == "en" else "Привет мир."), 10.0
        report = evaluate(manifest, MODELS[0], True, recognize)
        assert report["overall"]["cases"] == 2
        assert report["overall"]["wer"] == report["overall"]["cer"] == 0
        assert report["by_language"]["ru"]["wer"] == 0
        assert report["overall"]["real_time_factor"] > 0
        assert "Привет" not in json.dumps(report, ensure_ascii=False)
        bad = {"cases": [{"audio": "../outside.wav", "reference": "refs/1.txt",
                          "language": "en"}]}
        manifest.write_text(json.dumps(bad), encoding="utf-8")
        try:
            evaluate(manifest, MODELS[0], True, recognize)
            raise AssertionError("Traversal path was accepted")
        except ValueError:
            pass
        bad["cases"][0]["audio"] = "recordings/1.wav"
        bad["cases"][0]["language"] = "fr"
        manifest.write_text(json.dumps(bad), encoding="utf-8")
        try:
            evaluate(manifest, MODELS[0], True, recognize)
            raise AssertionError("Foreign language was accepted")
        except ValueError:
            pass
        items.append(items[0])
        manifest.write_text(json.dumps({"cases": items}), encoding="utf-8")
        try:
            evaluate(manifest, MODELS[0], True, recognize)
            raise AssertionError("Duplicate audio was accepted")
        except ValueError:
            pass
    print("local EN/RU ASR corpus evaluation self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--model", choices=MODELS, default=MODELS[0])
    parser.add_argument("--omit-model-id", action="store_true")
    parser.add_argument("--output", type=Path,
                        help="Optional local numeric-only JSON report")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.manifest is None:
        parser.error("--manifest is required")
    try:
        result = evaluate(args.manifest, args.model, args.omit_model_id)
    except (OSError, UnicodeError, json.JSONDecodeError, ValueError) as exc:
        parser.error(f"Private corpus failed: {type(exc).__name__}: {exc}")
    report = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        if args.output.exists():
            parser.error("Refusing to overwrite an existing metrics report")
        args.output.write_text(report + "\n", encoding="utf-8")
    print(report)


if __name__ == "__main__":
    main()
