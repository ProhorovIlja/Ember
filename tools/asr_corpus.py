#!/usr/bin/env python3
"""Local EN/RU ASR corpus validation without writing audio or transcripts.

The user supplies a JSON manifest pointing ONLY to existing local WAV and
reference TXT files. Requests go only to 127.0.0.1:43822; reports contain
numeric error/latency metrics, never the referenced words or filenames.
"""
import argparse
import io
import json
import math
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
            expected = frames * reader.getnchannels() * reader.getsampwidth()
            if expected > len(data) or expected > MAX_WAV:
                raise ValueError("WAV header declared more samples than available")
            if len(reader.readframes(frames)) != expected:
                raise ValueError("WAV sample data was truncated")
            return round(frames / rate, 3)
    except (EOFError, wave.Error) as exc:
        raise ValueError("Corrupt or unsupported RIFF WAV") from exc



def join_overlap_text(segments):
    """Ember's conservative lexical merger: only 2+ matching boundary words."""
    merged = []
    def key(word):
        while word and not word[0].isalnum():
            word = word[1:]
        while word and not word[-1].isalnum():
            word = word[:-1]
        return word.casefold()
    for source in segments:
        incoming = source.split()
        duplicate = 0
        for count in range(min(12, len(merged), len(incoming)), 1, -1):
            if all(key(merged[-count + i]) and
                   key(merged[-count + i]) == key(incoming[i])
                   for i in range(count)):
                duplicate = count
                break
        merged.extend(incoming[duplicate:])
    return " ".join(merged)


def quiet_aligned_fresh_frames(pcm, prefix_frames, fresh_frames, sample_rate):
    """Approximate the native 25ms RMS seam heuristic for 16kHz mono PCM16.

    A metric harness cannot substitute for native QAudioFormat path testing.
    Both implementations prefer unchanged 25s chunks unless a 125ms quiet
    interval is evident near their end. There is no model inference here.
    """
    if (sample_rate != 16000 or not 0 <= prefix_frames or
            fresh_frames <= 0 or len(pcm) != 2 * (prefix_frames + fresh_frames) or
            fresh_frames < sample_rate * 3):
        return fresh_frames
    window = max(1, sample_rate // 40)
    earliest = ((max(0, fresh_frames - sample_rate * 12 // 10)
                 + window - 1) // window) * window
    latest = ((fresh_frames - sample_rate * 60 // 1000) // window) * window
    if latest <= earliest + 5 * window:
        return fresh_frames
    values = []
    for pos in range(earliest, latest - window + 1, window):
        beginning = (prefix_frames + pos) * 2
        data = pcm[beginning:beginning + window * 2]
        # Only 25ms is loaded per iteration; this never decodes the full WAV
        # into a second RAM-sized array.
        from array import array
        samples = array("h")
        samples.frombytes(data)
        if len(samples) != window:
            return fresh_frames
        rms = math.sqrt(sum(s * s for s in samples) / window) / 32768
        values.append((pos, rms))
    if not values:
        return fresh_frames
    highest = max(v for _, v in values)
    if highest < 0.012 or len(values) < 6:
        return fresh_frames
    threshold = max(0.0025, highest * 0.14)
    start = None
    best = None
    def candidate(run_start, run_end):
        if run_start is None or run_end - run_start < 5 * window:
            return None
        return run_start + (run_end - run_start) // 2
    for pos, rms in values:
        if rms <= threshold:
            if start is None:
                start = pos
        elif start is not None:
            new = candidate(start, pos)
            if new is not None:
                best = new
            start = None
    if start is not None:
        new = candidate(start, values[-1][0] + window)
        if new is not None:
            best = new
    return best if best is not None and 0 < best < fresh_frames else fresh_frames


def app_chunked_pcm_wavs(wav_data):
    """Yield private 16kHz mono WAV chunks matching Ember's 25s/500ms policy.

    Refuse other input formats rather than pretending to match Ember's sinc
    resampler for 44.1/48k stereo microphone capture.
    """
    if len(wav_data) > MAX_WAV or len(wav_data) < 44:
        raise ValueError("Out-of-budget WAV is not valid for local chunking")
    with wave.open(io.BytesIO(wav_data), "rb") as reader:
        rate = reader.getframerate()
        if reader.getnchannels() != 1 or reader.getsampwidth() != 2 or rate != 16000:
            raise ValueError("--app-chunking requires pre-resampled 16kHz mono PCM16")
        total = reader.getnframes()
        if total <= 0 or total * 2 > MAX_WAV or total * 2 > len(wav_data):
            raise ValueError("WAV declares unsupported or oversized frame count")
        samples = reader.readframes(total)
        if len(samples) != total * 2:
            raise ValueError("Truncated WAV has fewer frames than declared")
    frames_per_chunk = rate * 25
    overlap_frames = rate // 2
    offset = 0
    while offset < total:
        wanted = min(frames_per_chunk, total - offset)
        start = max(0, offset - overlap_frames)
        end = offset + wanted
        pcm = samples[start * 2:end * 2]
        if end < total:
            wanted = quiet_aligned_fresh_frames(pcm, offset - start, wanted, rate)
            end = offset + wanted
            pcm = pcm[:(end - start) * 2]
        output = io.BytesIO()
        with wave.open(output, "wb") as writer:
            writer.setnchannels(1)
            writer.setsampwidth(2)
            writer.setframerate(rate)
            writer.writeframes(pcm)
        yield output.getvalue()
        offset = end


def evaluate(manifest, model, omit_model_id=False, recognizer=transcribe,
             app_chunking=False):
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
        if app_chunking:
            fragments = []
            elapsed_ms = 0.0
            for chunk in app_chunked_pcm_wavs(wav):
                text, duration_ms = recognizer(
                    chunk, model, language, omit_model_id=omit_model_id)
                fragments.append(text)
                elapsed_ms += duration_ms
            prediction = join_overlap_text(fragments)
            segment_count = len(fragments)
        else:
            prediction, elapsed_ms = recognizer(
                wav, model, language, omit_model_id=omit_model_id)
            segment_count = 1
        trial = metrics(expected, prediction)
        rows.append({
            "trial": index + 1, "language": language,
            "duration_seconds": duration, "asr_wall_ms": elapsed_ms,
            "segments": segment_count,
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
            "asr_segments": sum(x["segments"] for x in entries),
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
        "app_chunking_25s_500ms": app_chunking,
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
        # 32 seconds of synthetic PCM triggers the exact two-call local
        # segmentation shape (25s + 0.5s overlap + 7s).
        audio = io.BytesIO()
        with wave.open(audio, "wb") as writer:
            writer.setnchannels(1)
            writer.setsampwidth(2)
            writer.setframerate(16000)
            writer.writeframes(b"\x00\x00" * (32 * 16000))
        (root / "recordings" / "3.wav").write_bytes(audio.getvalue())
        (root / "refs" / "3.txt").write_text(
            "I walked along the old road and river.", encoding="utf-8")
        long_case = [{
            "audio": "recordings/3.wav", "reference": "refs/3.txt",
            "language": "en"
        }]
        manifest.write_text(json.dumps({"cases": long_case}), encoding="utf-8")
        calls = []
        def recognize_long(wav, _model, _language, omit_model_id=False):
            assert omit_model_id
            calls.append(wav)
            with wave.open(io.BytesIO(wav), "rb") as reader:
                frame_count = reader.getnframes()
                assert reader.getframerate() == 16000
                assert frame_count <= 25 * 16000 + 8000
            phrase = ("I walked along the old road" if len(calls) == 1
                      else "the old road and river.")
            return phrase, 25.0
        long_report = evaluate(manifest, MODELS[0], True, recognize_long, True)
        assert long_report["overall"]["wer"] == 0
        assert long_report["overall"]["asr_segments"] == len(calls) == 2
        assert long_report["overall"]["real_time_factor"] > 0
        assert "river" not in json.dumps(long_report)
        try:
            check_wave(audio.getvalue()[:-10])
            raise AssertionError("Truncated corpus WAV incorrectly accepted")
        except ValueError:
            pass
        try:
            list(app_chunked_pcm_wavs(audio.getvalue()[:-10]))
            raise AssertionError("Truncated long chunked WAV incorrectly accepted")
        except ValueError:
            pass
        assert join_overlap_text(["Сегодня я читаю эту книгу",
                                 "эту книгу каждый день"]) == (
                                 "Сегодня я читаю эту книгу каждый день")
        # Bounded silence alignment: audio with a distinct late quiet gap
        # is cut in that gap, unlike uninterrupted speech.
        from array import array
        tone = array("h", [6000]) * (25 * 16000)
        gap_start = 25 * 16000 - 11200
        gap_end = 25 * 16000 - 7200
        for i in range(gap_start, gap_end):
            tone[i] = 0
        tail = tone.tobytes()
        chosen = quiet_aligned_fresh_frames(tail, 0, len(tone), 16000)
        assert gap_start <= chosen <= gap_end
        loud = array("h", [6000]) * (25 * 16000)
        assert quiet_aligned_fresh_frames(
            loud.tobytes(), 0, len(loud), 16000) == len(loud)
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
    parser.add_argument("--app-chunking", action="store_true",
                        help="Benchmark Ember-style 25s/500ms overlapping segments; requires 16kHz mono WAV")
    parser.add_argument("--output", type=Path,
                        help="Optional local numeric-only JSON report")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.manifest is None:
        parser.error("--manifest is required")
    try:
        result = evaluate(args.manifest, args.model, args.omit_model_id,
                          app_chunking=args.app_chunking)
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
