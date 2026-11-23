#!/usr/bin/env python3
"""Blind A/B evaluation of real, manually supplied Ember TTS WAV recordings.

No synthesis, microphone, playback, network, model downloads, schedules or
automatic ranking by unreliable signal heuristics. Human listeners use their
own local audio player. Session WAVs and secret assignments remain private.
The score report contains preference votes only, never audio or transcripts.
"""
import argparse
import hashlib
import json
import os
import re
import secrets
import shutil
import struct
import tempfile
import wave
from pathlib import Path

SCHEMA = "ember-voice-blind-session-v1"
RESULT_SCHEMA = "ember-voice-blind-scores-v1"
CRITERIA = ("naturalness", "emotional_range", "clarity", "speaker_consistency")
ID = re.compile(r"[a-z0-9][a-z0-9_-]{0,47}\Z")
MAX_PAIRS = 40
MAX_MANIFEST = 128 * 1024
MAX_SESSION = 64 * 1024
MAX_SECONDS = 45

def reject(message):
    raise ValueError(message)

def checksum(payload):
    return hashlib.sha256(payload).hexdigest()

def exclusive(path, payload):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL |
                 getattr(os, "O_NOFOLLOW", 0), 0o600)
    with os.fdopen(fd, "wb") as handle:
        handle.write(payload)
        handle.flush()
        os.fsync(handle.fileno())

def no_duplicate_keys(pairs):
    result = {}
    for name, val in pairs:
        if name in result:
            reject("Duplicate JSON key in manifest/session")
        result[name] = val
    return result

def read_json(path, limit):
    file = Path(path)
    if (not file.is_file() or file.is_symlink() or
            not 0 < file.stat().st_size <= limit):
        reject("Missing or unsafe local JSON file")
    try:
        return json.loads(file.read_text(encoding="utf-8"),
                          object_pairs_hook=no_duplicate_keys,
                          parse_constant=lambda x: reject("Non-finite JSON constant"))
    except (json.JSONDecodeError, UnicodeError):
        reject("Invalid JSON input")

def original(root, name):
    if (not isinstance(name, str) or not 1 <= len(name) <= 240 or
            "\x00" in name):
        reject("Invalid WAV path")
    p = Path(name)
    if (p.is_absolute() or p.suffix.lower() != ".wav" or ".." in p.parts
            or any(part in ("", ".") for part in p.parts)):
        reject("Supply relative WAV paths inside the input folder")
    file = root / p
    resolved = file.resolve()
    if (not file.is_file() or file.is_symlink() or
            root not in resolved.parents or
            not 256 < file.stat().st_size <= MAX_SECONDS * 192000 * 2):
        reject("WAV file missing, outside folder, symlinked or too large")
    return file

def normalized_pcm(file):
    try:
        with wave.open(str(file), "rb") as reader:
            fmt = (reader.getnchannels(), reader.getsampwidth(),
                   reader.getframerate(), reader.getcomptype())
            channels, width, rate, comp = fmt
            frames = reader.getnframes()
            if (channels != 1 or width != 2 or comp != "NONE" or
                    rate not in (16000, 22050, 24000, 32000, 44100, 48000) or
                    not rate // 2 <= frames <= MAX_SECONDS * rate):
                reject("Only 0.5..45s mono PCM-16 WAV at a known rate is supported")
            pcm = reader.readframes(frames)
            if len(pcm) != frames * width:
                reject("Truncated WAV samples")
    except (wave.Error, EOFError, struct.error):
        reject("Invalid WAV container")
    if not any(pcm):
        reject("All-silent WAV cannot be rated")
    return rate, frames, pcm

def validated_manifest(root, manifest):
    data = read_json(manifest, MAX_MANIFEST)
    if (not isinstance(data, dict) or set(data) != {"schema", "baseline",
            "candidate", "pairs"} or data["schema"] != SCHEMA):
        reject("Invalid blind voice manifest schema")
    for name in ("baseline", "candidate"):
        label = data[name]
        if (not isinstance(label, str) or not 1 <= len(label) <= 80 or
                any(ord(c) < 32 for c in label)):
            reject("Model labels must be printable")
    if data["baseline"] == data["candidate"]:
        reject("Use two different model labels")
    rows = data["pairs"]
    if not isinstance(rows, list) or not 2 <= len(rows) <= MAX_PAIRS:
        reject("Need 2..40 paired WAV test cases")
    ids = set()
    prepared = []
    for row in rows:
        if not isinstance(row, dict) or set(row) != {"id", "baseline", "candidate"}:
            reject("Each pair needs id, baseline, candidate")
        case = row["id"]
        if not isinstance(case, str) or not ID.fullmatch(case) or case in ids:
            reject("Case IDs must be unique safe ASCII names")
        ids.add(case)
        pa = original(root, row["baseline"])
        pb = original(root, row["candidate"])
        arate, aframes, a = normalized_pcm(pa)
        brate, bframes, b = normalized_pcm(pb)
        if arate != brate:
            reject("Paired samples need the same PCM sampling rate")
        if a == b:
            reject("Identical paired audio; no comparison can be made")
        prepared.append({
            "id": case, "rate": arate,
            "baseline": a, "candidate": b,
            "baseline_sha256": checksum(a),
            "candidate_sha256": checksum(b),
        })
    return data, prepared

def wav_bytes(pcm, rate):
    import io
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(pcm)
    return buffer.getvalue()

def assigned_direction(index, size):
    # Deterministic balanced sides only in tests; production uses secrets.
    return index >= size // 2

def prepare(input_folder, manifest, output):
    root = Path(input_folder)
    if not root.is_dir() or root.is_symlink():
        reject("Input folder must be a real local directory")
    root = root.resolve()
    source, pairs = validated_manifest(root, manifest)
    dest = Path(output)
    if dest.exists() or dest.is_symlink() or not dest.parent.is_dir():
        reject("Output folder must be new with an existing parent")
    target = dest.resolve()
    if target == root or root in target.parents or target in root.parents:
        reject("Output folder must not overlap the private input WAV folder")
    n = len(pairs)
    # Balanced A/B order minimizes first-sample preference bias.
    directions = [False] * (n // 2) + [True] * (n - n // 2)
    secrets.SystemRandom().shuffle(directions)
    rows, output_audio = [], []
    for i, (record, reverse) in enumerate(zip(pairs, directions), 1):
        filename = f"P{i:03}"
        left = record["candidate"] if reverse else record["baseline"]
        right = record["baseline"] if reverse else record["candidate"]
        a_name, b_name = filename + "_A.wav", filename + "_B.wav"
        wa, wb = wav_bytes(left, record["rate"]), wav_bytes(right, record["rate"])
        output_audio.extend(((a_name, wa), (b_name, wb)))
        rows.append({
            "id": record["id"], "pair": filename,
            "candidate_is_a": reverse,
            "a_file": a_name, "b_file": b_name,
            "a_sha256": checksum(wa), "b_sha256": checksum(wb),
            "rate_hz": record["rate"]
        })
    key = {
        "schema": SCHEMA,
        "baseline_label": source["baseline"],
        "candidate_label": source["candidate"],
        "pairs": rows,
        "note": ("Keep this secret assignment file private until scoring; "
                 "WAV names hide model identity, not acoustic fingerprints. "
                 "Source filenames, model prompts and transcripts are not stored.")
    }
    dest.mkdir(mode=0o700)
    try:
        for name, wav in output_audio:
            exclusive(dest / name, wav)
        exclusive(dest / "private_blinding_key.json", (
            json.dumps(key, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        readme = (
            "EMBER VOICE — HUMAN BLIND LISTENING (LOCAL FILES ONLY)\n"
            "Do not inspect private_blinding_key.json before rating.\n"
            "Play each Pnnn_A.wav and Pnnn_B.wav using any local WAV player.\n"
            "Rate naturalness, emotion, clarity and speaker consistency.\n"
            "Then run: python tools/voice_listening_ab.py score "
            "--session <this-folder> --output <new-local-scores.json>\n"
            "No audio or transcript text is added to score reports.\n"
        )
        exclusive(dest / "LISTEN_FIRST.txt", readme.encode("utf-8"))
    except Exception:
        shutil.rmtree(dest)
        raise
    return {"pairs": n, "session": str(dest), "audio_files": 2 * n,
            "note": "Private anonymized WAV files created; human scoring not completed."}

def verified_session(folder):
    root = Path(folder)
    if not root.is_dir() or root.is_symlink():
        reject("Session folder must be local and not a symlink")
    key = read_json(root / "private_blinding_key.json", MAX_SESSION)
    if (not isinstance(key, dict) or key.get("schema") != SCHEMA
            or not isinstance(key.get("baseline_label"), str)
            or not isinstance(key.get("candidate_label"), str)):
        reject("Invalid private A/B session key")
    rows = key.get("pairs")
    if not isinstance(rows, list) or not 2 <= len(rows) <= MAX_PAIRS:
        reject("Invalid A/B session pair count")
    seen = set()
    for index, row in enumerate(rows, 1):
        if (not isinstance(row, dict) or set(row) != {
                "id", "pair", "candidate_is_a", "a_file", "b_file",
                "a_sha256", "b_sha256", "rate_hz"}
                or not isinstance(row["candidate_is_a"], bool)
                or not isinstance(row["id"], str) or not ID.fullmatch(row["id"])
                or row["id"] in seen
                or row["pair"] != f"P{index:03}"
                or row["a_file"] != f"P{index:03}_A.wav"
                or row["b_file"] != f"P{index:03}_B.wav"):
            reject("Invalid session assignment metadata")
        seen.add(row["id"])
        for prefix in ("a", "b"):
            file = root / row[prefix + "_file"]
            if (not file.is_file() or file.is_symlink()
                    or checksum(file.read_bytes()) != row[prefix + "_sha256"]):
                reject("Blind audio changed or went missing")
            rate, _, _ = normalized_pcm(file)
            if rate != row["rate_hz"]:
                reject("Blind audio sample rate changed")
    return key

def decode_preference(choice, candidate_is_a):
    if choice == "=":
        return "tie"
    if choice not in ("A", "B") or not isinstance(candidate_is_a, bool):
        reject("Invalid A/B preference")
    return "candidate" if (choice == "A") == candidate_is_a else "baseline"

def score(session, output, prompt=input):
    key = verified_session(session)
    target = Path(output)
    if target.exists() or target.is_symlink():
        reject("Output exists: not starting scoring")
    results = []
    print("Listen to each private A/B WAV pair first. Labels remain hidden.")
    for row in key["pairs"]:
        print("\nPAIR", row["pair"],
              "Listen to", row["a_file"], "and", row["b_file"])
        rating = {"id": row["id"]}
        for criterion in CRITERIA:
            while True:
                choice = prompt(criterion + " (A / B / = tie): ").strip().upper()
                if choice in ("A", "B", "="):
                    break
                print("Enter A, B or =")
            rating[criterion] = decode_preference(
                choice, row["candidate_is_a"])
        results.append(rating)
    counts = {criterion: {
        label: sum(r[criterion] == label for r in results)
        for label in ("baseline", "candidate", "tie")
    } for criterion in CRITERIA}
    report = {
        "schema": RESULT_SCHEMA,
        "baseline": key["baseline_label"],
        "candidate": key["candidate_label"],
        "pairs": len(results),
        "criteria": list(CRITERIA),
        "results": results, "counts": counts,
        "note": ("Human subjective A/B preferences only. This file contains "
                 "no WAV bytes, transcripts or input WAV paths. No inference "
                 "about intelligence or physical first-sound latency.")
    }
    exclusive(target, (json.dumps(report, ensure_ascii=False, indent=2)
                       + "\n").encode("utf-8"))
    print("Saved score-only report:", target)
    return report

def self_test():
    assert decode_preference("A", False) == "baseline"
    assert decode_preference("B", False) == "candidate"
    assert decode_preference("A", True) == "candidate"
    assert decode_preference("B", True) == "baseline"
    assert decode_preference("=", False) == "tie"
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        source = root / "input"
        source.mkdir()
        records = []
        for index in range(4):
            pa, pb = f"t{index}_base.wav", f"t{index}_new.wav"
            for name, sample in ((pa, 800 + index * 17),
                                 (pb, 1600 + index * 19)):
                pcm = struct.pack("<h", sample) * 24000
                (source / name).write_bytes(wav_bytes(pcm, 24000))
            records.append({"id": "case-" + str(index),
                            "baseline": pa, "candidate": pb})
        manifest = root / "manifest.json"
        manifest.write_text(json.dumps({
            "schema": SCHEMA, "baseline": "model-one",
            "candidate": "model-two", "pairs": records}))
        out = root / "blind"
        prepared = prepare(source, manifest, out)
        assert prepared["audio_files"] == 8
        key = verified_session(out)
        assert len(key["pairs"]) == 4
        assert sum(r["candidate_is_a"] for r in key["pairs"]) == 2
        answers = iter(["A", "B", "=", "A"] * 4)
        result = score(out, root / "scores.json",
                       prompt=lambda _question: next(answers))
        assert result["pairs"] == 4
        assert sum(result["counts"]["naturalness"].values()) == 4
        raw = (root / "scores.json").read_text()
        assert "RIFF" not in raw
        assert "t0_base.wav" not in raw and "t0_new.wav" not in raw
        try:
            prepare(source, manifest, out)
            raise AssertionError("Session overwrite accepted")
        except ValueError:
            pass
        try:
            score(out, root / "scores.json", prompt=lambda _: "A")
            raise AssertionError("Scores overwritten")
        except ValueError:
            pass
        pair = key["pairs"][0]
        file = out / pair["a_file"]
        original_bytes = file.read_bytes()
        file.write_bytes(original_bytes[:-2] + b"\x00\x00")
        try:
            verified_session(out)
            raise AssertionError("Modified WAV accepted")
        except ValueError:
            pass
        file.write_bytes(original_bytes)
        bad = dict(records[0], candidate=records[0]["baseline"])
        manifest.write_text(json.dumps({
            "schema": SCHEMA, "baseline": "one",
            "candidate": "two", "pairs": [bad, records[1]]}))
        try:
            prepare(source, manifest, root / "identical")
            raise AssertionError("Identical paired audio accepted")
        except ValueError:
            pass
    print("Ember voice human blind A/B self-test PASS (synthetic PCM only)")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    prep = sub.add_parser("prepare", help="Make anonymized local WAV pairs")
    prep.add_argument("--input-dir", required=True, type=Path)
    prep.add_argument("--manifest", required=True, type=Path)
    prep.add_argument("--output-dir", required=True, type=Path)
    scoring = sub.add_parser("score", help="Manually score private A/B session")
    scoring.add_argument("--session", required=True, type=Path)
    scoring.add_argument("--output", required=True, type=Path)
    sub.add_parser("self-test")
    args = parser.parse_args()
    try:
        if args.command == "prepare":
            print(json.dumps(prepare(args.input_dir, args.manifest,
                                     args.output_dir), indent=2))
        elif args.command == "score":
            score(args.session, args.output)
        else:
            self_test()
    except (ValueError, OSError, TypeError, UnicodeError) as error:
        parser.error(str(error))

if __name__ == "__main__":
    main()
