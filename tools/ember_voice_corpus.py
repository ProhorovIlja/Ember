#!/usr/bin/env python3
"""Prepare consent-declared single-speaker WAV data for Qwen3-TTS fine-tuning.

No model, network, audio transcription, synthetic voice, conversion, cloud
storage, training, background jobs or scheduled action is invoked.
"""
import argparse
import collections
import hashlib
import json
import math
import os
import struct
import tempfile
import wave
from pathlib import Path

SCHEMA = "ember-voice-source-v1"
REPORT = "ember-voice-corpus-report-v1"
SEED = "ember-voice-by-session-holdout-v1"
RATE = 24000
MAX_MANIFEST = 2 * 1024 * 1024

def fail(reason):
    raise ValueError(reason)

def digest(path):
    h = hashlib.sha256()
    with Path(path).open("rb") as reader:
        for block in iter(lambda: reader.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()

def distinct_keys(pairs):
    value = {}
    for key, val in pairs:
        if key in value:
            fail("Duplicate manifest JSON key")
        value[key] = val
    return value

def local_audio(root, relative):
    if not isinstance(relative, str) or not relative or len(relative) > 240:
        fail("Invalid audio path")
    ref = Path(relative)
    if ref.is_absolute() or ".." in ref.parts or ref.suffix.lower() != ".wav":
        fail("Only relative WAV files within source folder are accepted")
    path = root / ref
    if not path.is_file() or path.is_symlink() or root not in path.resolve().parents:
        fail("Missing or unsafe audio file")
    if not 512 < path.stat().st_size < 3 * 1024 * 1024:
        fail("WAV size outside bounded single-utterance range")
    return path.resolve()

def check_wave(path):
    try:
        with wave.open(str(path), "rb") as reader:
            if (reader.getnchannels() != 1 or reader.getsampwidth() != 2 or
                    reader.getframerate() != RATE or reader.getcomptype() != "NONE"):
                fail("Training audio must be 24kHz mono 16-bit PCM WAV")
            frames = reader.getnframes()
            if frames < RATE or frames > RATE * 25:
                fail("Audio must be 1 to 25 seconds per utterance")
            samples = reader.readframes(frames)
    except (wave.Error, EOFError):
        fail("Invalid PCM WAV container")
    if len(samples) != frames * 2:
        fail("Incomplete WAV data")
    values = struct.iter_unpack("<h", samples)
    squared = 0
    peaks = 0
    for (v,) in values:
        squared += v * v
        peaks += abs(v) >= 32000
    rms = math.sqrt(squared / frames)
    if rms < 250 or peaks / frames > 0.02:
        fail("Severely silent or clipped audio")
    return round(frames / RATE, 3)

def load_manifest(root, manifest):
    source = Path(manifest)
    if not source.is_file() or source.is_symlink() or not 0 < source.stat().st_size < MAX_MANIFEST:
        fail("Source JSONL manifest missing or too large")
    rows, seen, audio_hashes = [], set(), set()
    with source.open(encoding="utf-8") as reader:
        for index, line in enumerate(reader, 1):
            if index > 2000 or not line.strip() or len(line) > 12000:
                fail("Invalid source manifest length")
            try:
                obj = json.loads(line, object_pairs_hook=distinct_keys,
                                 parse_constant=lambda x: fail("Invalid JSON number"))
            except json.JSONDecodeError:
                fail("Malformed source JSONL")
            if not isinstance(obj, dict) or set(obj) != {
                    "schema", "audio", "text", "session", "consent"}:
                fail("Manifest rows need schema, audio, text, session, consent")
            if obj["schema"] != SCHEMA or obj["consent"] is not True:
                fail("Every recording requires a positive consent declaration")
            session = obj["session"]
            if (not isinstance(session, str) or not 1 <= len(session) <= 80
                    or any(ord(c) < 32 for c in session)):
                fail("Invalid session/group label")
            transcript = obj["text"]
            if (not isinstance(transcript, str) or
                    not 3 <= len(transcript.strip()) <= 500 or
                    any(c in transcript for c in ("\x00", "\n", "\r")) or
                    sum(c.isalpha() for c in transcript) < 3):
                fail("Need actual manually verified English transcription")
            name = obj["audio"]
            if name in seen:
                fail("Duplicate audio file in voice corpus")
            seen.add(name)
            wav = local_audio(root, name)
            duration = check_wave(wav)
            checksum = digest(wav)
            if checksum in audio_hashes:
                fail("Identical WAV contents cannot be repeated under another name")
            audio_hashes.add(checksum)
            rows.append({
                "audio": str(wav), "text": transcript,
                "session": session, "duration": duration,
                "sha256": checksum})
    if len(rows) < 4:
        fail("Need at least four consent-declared utterances")
    sessions = {r["session"] for r in rows}
    if len(sessions) < 2:
        fail("Need two independent recording sessions for a private holdout")
    return rows

def split(rows):
    groups = sorted({r["session"] for r in rows},
                    key=lambda x: hashlib.sha256((SEED + x).encode()).hexdigest())
    count = max(1, min(len(groups) - 1, round(len(groups) * 0.2)))
    eval_groups = set(groups[:count])
    return {
        key: [r for r in rows if (r["session"] in eval_groups) == (key == "eval")]
        for key in ("train", "eval")
    }

def write_exclusive(path, content):
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
    with os.fdopen(os.open(path, flags, 0o600), "wb") as writer:
        writer.write(content)
        writer.flush()
        os.fsync(writer.fileno())

def prepare(audio_root, manifest, ref, output):
    source_root = Path(audio_root)
    if not source_root.is_dir() or source_root.is_symlink():
        fail("Audio source must be a real local directory")
    source_root = source_root.resolve()
    reference = local_audio(source_root, ref)
    check_wave(reference)
    rows = load_manifest(source_root, manifest)
    # A reference clip is a real speaker recording, not metadata. Insist
    # that it has an explicit positive consent row in the source manifest.
    if sum(row["audio"] == str(reference) for row in rows) != 1:
        fail("Reference audio requires its own consent-declared manifest row")
    buckets = split(rows)
    if any(not bucket for bucket in buckets.values()):
        fail("Train and evaluation split cannot be empty")
    if any(row["audio"] == str(reference) for row in buckets["eval"]):
        fail("Reference voice clip belongs to the evaluation session; "
             "choose a consented reference from a training session")
    destination = Path(output)
    if destination.exists() or destination.is_symlink() or not destination.parent.is_dir():
        fail("Output must be a new folder below an existing parent")
    dest = destination.resolve()
    if dest == source_root or source_root in dest.parents or dest in source_root.parents:
        fail("Output and raw audio directories must be separate")
    written = {}
    summary = {
        "schema": REPORT,
        "source_manifest_sha256": digest(manifest),
        "speaker_reference_sha256": digest(reference),
        "target_format": "PCM-16 mono 24000 Hz",
        "single_speaker_consent_asserted": True,
        "split_seed": SEED,
        "total_recordings": len(rows),
        "train_recordings": len(buckets["train"]),
        "eval_recordings": len(buckets["eval"]),
        "train_seconds": round(sum(x["duration"] for x in buckets["train"]), 2),
        "eval_seconds": round(sum(x["duration"] for x in buckets["eval"]), 2),
        "recording_hashes": [r["sha256"] for r in rows],
        "model_training_performed": False,
        "caution": ("Official single-speaker Qwen3-TTS input format. Consent "
                    "and matching speaker identity must be verified by the "
                    "operator. Local JSONL lists plaintext transcripts and "
                    "audio paths. Neither training nor listening QA occurred.")
    }
    for name, bucket in buckets.items():
        lines = []
        for record in bucket:
            lines.append(json.dumps({
                "audio": record["audio"], "text": record["text"],
                "ref_audio": str(reference)}, ensure_ascii=False) + "\n")
        written[name] = "".join(lines).encode("utf-8")
        summary[name + "_sha256"] = hashlib.sha256(written[name]).hexdigest()
    dest.mkdir(mode=0o700)
    try:
        for name in ("train", "eval"):
            write_exclusive(dest / (name + "_raw.jsonl"), written[name])
        write_exclusive(dest / "report.json", (
            json.dumps(summary, ensure_ascii=False, indent=2) + "\n"
        ).encode("utf-8"))
    except Exception:
        for name in ("train_raw.jsonl", "eval_raw.jsonl", "report.json"):
            target = dest / name
            if target.is_file() and not target.is_symlink():
                target.unlink()
        dest.rmdir()
        raise
    return summary

def self_test():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        audio = root / "audio"
        audio.mkdir()
        def make_wav(path, hz):
            with wave.open(str(path), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                frames = bytearray()
                for i in range(RATE * 2):
                    sample = round(9500 * math.sin(2 * math.pi * hz * i / RATE))
                    frames.extend(struct.pack("<h", sample))
                w.writeframes(frames)
        make_wav(audio / "ref.wav", 530)
        source = root / "source.jsonl"
        examples = []
        for index in range(6):
            name = "clip-" + str(index) + ".wav"
            make_wav(audio / name, 250 + index * 37)
            examples.append({
                "schema": SCHEMA, "audio": name,
                "text": "A normal expressive English test sentence.",
                "session": "session-a" if index < 3 else "session-b",
                "consent": True})
        # Determine the training-side session from the same deterministic
        # session split; the consented reference must never leak from eval.
        initial = [{"session": item["session"]} for item in examples]
        eval_name = split(initial)["eval"][0]["session"]
        train_name = "session-b" if eval_name == "session-a" else "session-a"
        examples.append({
            "schema": SCHEMA, "audio": "ref.wav",
            "text": "This is the consented reference voice recording.",
            "session": train_name, "consent": True})
        source.write_text("".join(json.dumps(x) + "\n" for x in examples))
        a = prepare(audio, source, "ref.wav", root / "out-a")
        b = prepare(audio, source, "ref.wav", root / "out-b")
        assert a["train_recordings"] + a["eval_recordings"] == 7
        assert a["train_sha256"] == b["train_sha256"]
        assert a["eval_sha256"] == b["eval_sha256"]
        assert a["speaker_reference_sha256"] == b["speaker_reference_sha256"]
        report = (root / "out-a" / "report.json").read_text()
        assert "A normal expressive English" not in report
        assert "A normal expressive English" in (
            root / "out-a" / "train_raw.jsonl").read_text()
        try:
            prepare(audio, source, "ref.wav", root / "out-a")
            raise AssertionError("Existing voice corpus overwritten")
        except ValueError:
            pass
        without_reference = root / "no-ref.jsonl"
        without_reference.write_text("".join(
            json.dumps(x) + "\n" for x in examples if x["audio"] != "ref.wav"))
        try:
            prepare(audio, without_reference, "ref.wav", root / "missing-ref-consent")
            raise AssertionError("Unconsented reference voice accepted")
        except ValueError:
            assert not (root / "missing-ref-consent").exists()
        # Reusing an eval-side clip as the reference is also forbidden.
        eval_audio = (root / "out-a" / "eval_raw.jsonl").read_text().splitlines()
        eval_ref = Path(json.loads(eval_audio[0])["audio"]).name
        try:
            prepare(audio, source, eval_ref, root / "eval-ref")
            raise AssertionError("Evaluation recording used as voice reference")
        except ValueError:
            assert not (root / "eval-ref").exists()
        examples[0]["consent"] = False
        source.write_text("".join(json.dumps(x) + "\n" for x in examples))
        try:
            prepare(audio, source, "ref.wav", root / "invalid")
            raise AssertionError("Non-consenting data accepted")
        except ValueError:
            assert not (root / "invalid").exists()
    print("Ember Voice consent, PCM and split self-test PASS (synthetic audio only)")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare")
    prep.add_argument("--audio-root", type=Path, required=True)
    prep.add_argument("--manifest", type=Path, required=True)
    prep.add_argument("--reference", required=True,
                      help="Clean reference WAV relative to audio root")
    prep.add_argument("--output-dir", type=Path, required=True)
    sub.add_parser("self-test")
    args = p.parse_args()
    try:
        if args.command == "self-test":
            self_test()
        else:
            print(json.dumps(prepare(args.audio_root, args.manifest, args.reference,
                                     args.output_dir), ensure_ascii=False, indent=2))
    except (ValueError, OSError, TypeError, UnicodeError) as exc:
        p.error(str(exc))

if __name__ == "__main__":
    main()
