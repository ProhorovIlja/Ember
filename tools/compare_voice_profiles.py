#!/usr/bin/env python3
"""Compare paired localhost Ember TTS signal benchmarks; no fake MOS ranking.

Only compare runs with the exact same prompts, seeds, sample count and
manually asserted hardware/LLM-load label. This reads numeric JSON reports;
it never reads or emits recorded speech, transcripts or private chat text.
"""
import argparse
import json
import math
import statistics
import tempfile
from pathlib import Path


def load_report(path):
    file = Path(path)
    if not file.is_file() or file.stat().st_size > 256 * 1024:
        raise ValueError("Missing or oversized voice metric report")
    data = json.loads(file.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("benchmark_schema_version") != 2:
        raise ValueError("Benchmark reports must use matching schema v2")
    trials = data.get("trials")
    if (not isinstance(trials, list) or not 1 <= len(trials) <= 40 or
            data.get("samples") != len(trials) or
            not isinstance(data.get("prompt_set_sha256"), str) or
            len(data["prompt_set_sha256"]) != 64):
        raise ValueError("Benchmark trial count or prompt fingerprint is invalid")
    return data


def median(values):
    return round(statistics.median(values), 3) if values else None


def p95(values):
    return round(sorted(values)[math.ceil(0.95 * len(values)) - 1], 3) if values else None


def validated_numbers(rows, key, allow_missing=False):
    values = []
    for r in rows:
        value = r.get(key)
        if value is None and allow_missing:
            continue
        if (not isinstance(value, (int, float)) or isinstance(value, bool) or
                not math.isfinite(value) or value < 0):
            raise ValueError(f"Metric {key} is absent or invalid")
        values.append(value)
    return values


def compare(a, b):
    for label, data in (("baseline", a), ("candidate", b)):
        if data.get("benchmark_schema_version") != 2:
            raise ValueError(f"Unsupported {label} schema")
    if (a.get("prompt_set_sha256") != b.get("prompt_set_sha256") or
            a.get("environment_label") != b.get("environment_label") or
            not a.get("environment_label") or
            a["environment_label"] == "unknown" or
            len(a["trials"]) != len(b["trials"])):
        raise ValueError("Runs must share known environment, prompts and trial count")
    for previous, next_ in zip(a["trials"], b["trials"]):
        if (previous.get("prompt_index") != next_.get("prompt_index") or
                previous.get("seed") != next_.get("seed") or
                previous.get("sample_number") != next_.get("sample_number")):
            raise ValueError("Unpaired prompt/seed schedule: comparison refused")
    summary = {}
    for label, data in (("baseline", a), ("candidate", b)):
        rows = data["trials"]
        heard = sum(row.get("digital_voice_detected") is True for row in rows)
        if any(row.get("digital_voice_detected") not in (True, False) for row in rows):
            raise ValueError("Malformed digital voice detection flag")
        summary[label] = {
            "model": data["model"], "voice": data["voice"],
            "delivery": data["delivery"],
            "trials": len(rows), "above_threshold_trials": heard,
            "p50_first_http_byte_ms": median(validated_numbers(
                rows, "first_http_pcm_byte_ms")),
            "p95_first_http_byte_ms": p95(validated_numbers(
                rows, "first_http_pcm_byte_ms")),
            "p95_leading_digital_silence_ms": p95(validated_numbers(
                rows, "leading_low_level_pcm_ms", allow_missing=True)),
            "p95_clipped_sample_percent": p95(validated_numbers(
                rows, "clipped_sample_percent")),
            "mean_audio_seconds_per_wall_second": round(statistics.mean(
                validated_numbers(rows, "audio_seconds_per_wall_second")), 3),
        }
    return {
        "note": ("Numbers are matched digital signal/HTTP diagnostics only. "
                 "They do NOT measure physical first sound, naturalness, intelligibility, "
                 "voice preference or memory co-residency; blind listening and ASR needed."),
        "environment_label": a["environment_label"],
        "source_prompt_fingerprint": a["prompt_set_sha256"],
        "same_prompts_and_seeds": True,
        "baseline": summary["baseline"],
        "candidate": summary["candidate"],
    }


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        def run(voice, first):
            return {
                "benchmark_schema_version": 2, "prompt_set_sha256": "a" * 64,
                "environment_label": "win10-rtx4070-llm-resident", "samples": 2,
                "model": "qwen3-tts-1.7b", "voice": voice, "delivery": "natural",
                "trials": [
                    {"sample_number": i+1, "prompt_index": i, "seed": i+1,
                     "digital_voice_detected": True,
                     "first_http_pcm_byte_ms": first + i,
                     "leading_low_level_pcm_ms": 15.0,
                     "clipped_sample_percent": 0.0,
                     "audio_seconds_per_wall_second": 2.5}
                    for i in range(2)
                ]
            }
        root = Path(tmp)
        p = root / "vivian.json"
        q = root / "serena.json"
        p.write_text(json.dumps(run("vivian", 30.0)), encoding="utf-8")
        q.write_text(json.dumps(run("serena", 20.0)), encoding="utf-8")
        report = compare(load_report(p), load_report(q))
        assert report["same_prompts_and_seeds"]
        assert report["candidate"]["p50_first_http_byte_ms"] == 20.5
        invalid = load_report(q)
        invalid["trials"][0]["seed"] = 99
        try:
            compare(load_report(p), invalid)
            raise AssertionError("Mismatched seed was accepted")
        except ValueError:
            pass
        invalid = load_report(q)
        invalid["environment_label"] = "unknown"
        try:
            compare(load_report(p), invalid)
            raise AssertionError("Unmatched hardware was accepted")
        except ValueError:
            pass
    print("matched local TTS profile comparison self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--candidate", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.baseline or not args.candidate:
        parser.error("--baseline and --candidate are required")
    try:
        result = compare(load_report(args.baseline), load_report(args.candidate))
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        parser.error(f"Cannot compare voice measurements: {type(error).__name__}: {error}")
    output = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        if args.output.exists():
            parser.error("Refusing to overwrite existing comparison report")
        args.output.write_text(output + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
