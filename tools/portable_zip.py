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
from offline_bundle import verify


def archive(root, output):
    folder = Path(root).resolve(strict=True)
    expected = folder / "offline-assets.json"
    verify(folder, expected, approved=True)
    for required in ("Ember.exe", "Qt6Core.dll", "LICENSE"):
        if not (folder / required).is_file():
            raise ValueError(f"Missing complete native app: {required}")
    files = []
    for p in folder.rglob("*"):
        if p.is_symlink():
            raise ValueError("Symlink in offline portable payload")
        if p.is_dir():
            continue
        if not p.is_file():
            raise ValueError("Non-regular portable asset")
        relative = p.relative_to(folder).as_posix()
        if relative.split("/")[0].casefold() in ("data", "cache"):
            raise ValueError("Private conversations/audio must never enter portable ZIP")
        files.append((relative, p))
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
    print("portable_zip ZIP64 packaging self-test passed (synthetic PE/GGUF/GGML headers; not real weights)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        else:
            if args.root is None or args.output is None:
                parser.error("--root and --output are required")
            files, sha = archive(args.root, args.output)
            print(f"ZIP64 portable archived {files} files. SHA256: {sha}")
    except (OSError, ValueError, zipfile.BadZipFile) as exc:
        parser.error(f"{type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
