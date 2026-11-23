#!/usr/bin/env python3
"""Create a reproducibly ordered ZIP64-capable offline Ember portable archive.

No network, pip, model loading, preview publishing, or source code execution.
"""
import argparse
import hashlib
import os
from pathlib import Path
import tempfile
import zipfile
from offline_bundle import (verify, is_link_or_junction, private_payload_path,
                            WINDOWS_RESERVED)


def validate_portable_members(names):
    """Fail closed on Windows filename aliases across app, DLL and model files.

    Manifests validate engines/models, but the full Qt application directory
    can contain additional files. On case-insensitive Windows extraction,
    A.txt and a.txt (or a file and a same-named directory) collide.
    """
    if len(names) > 20000:
        raise ValueError("Offline portable payload contains too many files")
    files, directories = set(), set()
    for name in names:
        if not isinstance(name, str) or not 0 < len(name) <= 240:
            raise ValueError("Invalid Windows portable member name")
        segments = name.split("/")
        if any(not part or part in (".", "..") or part.endswith((" ", ".")) or
               any(ord(ch) < 32 or ch in '\\:*?"<>|' for ch in part) or
               part.split(".", 1)[0].upper() in WINDOWS_RESERVED
               for part in segments):
            raise ValueError("Unsafe Windows portable member: " + name)
        key_parts = [part.casefold() for part in segments]
        key = "/".join(key_parts)
        if key in files:
            raise ValueError("Case-insensitive Windows archive member collision")
        files.add(key)
        for depth in range(1, len(key_parts)):
            directories.add("/".join(key_parts[:depth]))
    if files.intersection(directories):
        raise ValueError("Windows portable archive has file/directory name collision")


def archive(root, output, *, require_voice=False):
    folder = Path(root).resolve(strict=True)
    expected = folder / "offline-assets.json"
    verify(folder, expected, approved=True, require_voice=require_voice)
    for required in ("Ember.exe", "Qt6Core.dll", "LICENSE"):
        if not (folder / required).is_file():
            raise ValueError(f"Missing complete native app: {required}")
    files = []
    for p in folder.rglob("*"):
        if is_link_or_junction(p):
            raise ValueError("Link or junction in offline portable payload")
        if p.is_dir():
            continue
        if not p.is_file():
            raise ValueError("Non-regular portable asset")
        relative = p.relative_to(folder).as_posix()
        # One shared policy applies to app files, staged models, and ZIP64;
        # refuse secret paths at any depth, not just the root directory.
        if private_payload_path(relative):
            raise ValueError("Sensitive data or smoke diagnostic in portable ZIP")
        files.append((relative, p))
    validate_portable_members([name for name, _ in files])
    files.sort(key=lambda pair: pair[0].casefold())
    dest = Path(output)
    if dest.exists():
        raise ValueError("Refusing to overwrite existing ZIP")
    dest.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".ember-zip64-", suffix=".zip",
                                         dir=dest.parent, delete=False) as handle:
            temporary = Path(handle.name)
        with zipfile.ZipFile(temporary, "w", allowZip64=True,
                             compression=zipfile.ZIP_DEFLATED, compresslevel=6) as file:
            for relative, path in files:
                # Large quantized weights are often incompressible. Avoid
                # wasting memory, CPU and installation time recompressing them.
                compression = (zipfile.ZIP_STORED if path.suffix.lower()
                               in (".gguf", ".onnx", ".bin") else zipfile.ZIP_DEFLATED)
                file.write(path, arcname=relative, compress_type=compression)
        with zipfile.ZipFile(temporary, "r", allowZip64=True) as file:
            if set(file.namelist()) != {name for name, _ in files}:
                raise ValueError("ZIP directory table verification failed")
        temporary.rename(dest)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    digest = hashlib.sha256()
    with dest.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return len(files), digest.hexdigest()


def self_test():
    # These are pure string tests so they run on Windows, where invalid
    # filenames cannot be constructed as real filesystem entries.
    validate_portable_members(["Ember.exe", "Qt6Core.dll", "qml/Main.qml"])
    for bad in (
        ["Ember.exe", "ember.EXE"],
        ["a.txt", "A.TXT/nested.txt"],
        ["docs/CON.txt"],
        ["plugins/trailing."],
        ["plugins/trailing "],
        ["plugins/C:stream"],
        ["plugins\\outside.dll"],
    ):
        try:
            validate_portable_members(bad)
            raise AssertionError("Unsafe Windows ZIP members accepted: " + repr(bad))
        except ValueError:
            pass
    from offline_bundle import inventory, stage, assemble, fake_pe64, fake_gguf, fake_whisper
    import json
    with tempfile.TemporaryDirectory() as temp:
        base = Path(temp)
        raw = base / "source"
        for name, body, component in (
            ("engines/llama/llama-server.exe", fake_pe64(), "llm"),
            ("models/llm/model.gguf", fake_gguf(), "llm"),
            ("engines/whisper/whisper-cli.exe", fake_pe64(), "asr"),
            ("models/stt/ggml-base.bin", fake_whisper(), "asr"),
            ("engines/speech/speech.exe", fake_pe64(), "tts"),
            ("models/speech/qwen3-tts-0.6b.gguf", fake_gguf(), "tts"),
            ("licenses/all.txt", b"synthetic testing license", "license"),
        ):
            target = raw / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
        manifest = base / "inventory.json"
        inventory(raw, manifest)
        data = json.loads(manifest.read_text(encoding="utf-8"))
        for item in data["assets"]:
            item["component"] = ("llm" if "/llm/" in item["path"] or
                                "/llama/" in item["path"] else
                                "asr" if "/stt/" in item["path"] or
                                "/whisper/" in item["path"] else
                                "tts" if "/speech/" in item["path"] else "license")
            item["redistribution_approved"] = True
            item["source_url"] = "https://example.org/synthetic-only"
            if not item["path"].startswith("licenses/"):
                item["license_path"] = "licenses/all.txt"
        approved = base / "approved.json"
        approved.write_text(json.dumps(data), encoding="utf-8")
        stage(raw, approved, base / "assets")
        app = base / "qt"
        app.mkdir()
        for name in ("Ember.exe", "Qt6Core.dll", "LICENSE"):
            (app / name).write_bytes(b"mock app")
        complete = assemble(base / "assets", base / "assets/offline-assets.json",
                            app, base / "complete")
        count, sha = archive(complete, base / "portable.zip")
        assert count >= 10 and len(sha) == 64
        # A model merely RENAMED to the expected voice GGUF alias is not
        # a compatible Qwen3-TTS model and must never ship as "complete".
        try:
            archive(complete, base / "fake-voice.zip", require_voice=True)
            raise AssertionError("Stable ZIP accepted a fake TTS model")
        except ValueError:
            pass
        # Positive structural path: a genuine speech.cpp metadata schema
        # passes the Qwen3-TTS identity gate. Synthetic tensor data does NOT
        # prove native voice inference or English prosody on target hardware.
        from native_gguf_metadata import _fixture_gguf
        voice_name = "models/speech/qwen3-tts-0.6b.gguf"
        voice_file = complete / voice_name
        voice_bytes = _fixture_gguf("qwen3-tts", "0.6B", "synthesis")
        voice_file.write_bytes(voice_bytes)
        revised = json.loads(json.dumps(data))
        for item in revised["assets"]:
            if item["path"] == voice_name:
                item["bytes"] = len(voice_bytes)
                item["sha256"] = hashlib.sha256(voice_bytes).hexdigest()
        (complete / "offline-assets.json").write_text(
            json.dumps(revised, ensure_ascii=False), encoding="utf-8")
        voice_count, voice_sha = archive(
            complete, base / "voice-approved.zip", require_voice=True)
        assert voice_count == count and len(voice_sha) == 64
        with zipfile.ZipFile(base / "voice-approved.zip") as approved_zip:
            assert approved_zip.read(voice_name)[:4] == b"GGUF"
        with zipfile.ZipFile(base / "portable.zip") as zipped:
            assert zipped.read("Ember.exe") == b"mock app"
            assert zipped.read("models/llm/model.gguf")[:8] == fake_gguf()[:8]
            assert zipped.getinfo("models/llm/model.gguf").compress_type == zipfile.ZIP_STORED
            assert "offline-assets.json" in zipped.namelist()
        try:
            archive(complete, base / "portable.zip")
            raise AssertionError("Overwriting a ZIP was allowed")
        except ValueError:
            pass
        for name in ("data/ember-chats.sqlite", "qml-smoke-report.txt",
                     "settings/credentials.json", "logs/session.pem"):
            candidate = complete / name
            candidate.parent.mkdir(parents=True, exist_ok=True)
            candidate.write_bytes(b"private information")
            try:
                archive(complete, base / ("must-reject-" +
                        name.replace("/", "-") + ".zip"))
                raise AssertionError("Private file was accepted in a ZIP: " + name)
            except ValueError:
                pass
            candidate.unlink()
    print("portable_zip ZIP64 packaging self-test passed (synthetic headers and private-file exclusions)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--require-voice", action="store_true",
                        help="Explicitly enforce the default: complete Ember must contain licensed TTS")
    parser.add_argument("--allow-text-only-development", action="store_true",
                        help="Internal development only; cannot represent a complete Ember release")
    args = parser.parse_args()
    if args.require_voice and args.allow_text_only_development:
        parser.error("Complete voice and text-only development flags conflict")
    try:
        if args.self_test:
            self_test()
        else:
            if args.root is None or args.output is None:
                parser.error("--root and --output are required")
            files, sha = archive(
                args.root, args.output,
                require_voice=not args.allow_text_only_development)
            print(f"ZIP64 portable archived {files} files. SHA256: {sha}")
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        parser.error(f"{type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
