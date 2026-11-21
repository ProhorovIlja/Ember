#!/usr/bin/env python3
"""Ember offline payload inventory/verification/staging. Uses no network or pip packages.

The manifest is an integrity and review record, not a legal license decision.
Assets must already be present on disk; nothing is downloaded or executed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile
from urllib.parse import urlsplit

SCHEMA = "ember-offline-assets-v1"
ROOTS = ("engines", "models", "licenses")
COMPONENTS = ("llm", "asr", "tts", "shared", "license")
MAX_FILES = 6000
MAX_MANIFEST = 3 * 1024 * 1024
MAX_FILE = 100 * 1024 ** 3
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
WINDOWS_RESERVED = {"CON", "PRN", "AUX", "NUL"} | {
    f"{letter}{number}" for letter in ("COM", "LPT") for number in range(1, 10)
}


def safe_path(value):
    if not isinstance(value, str) or len(value) > 240 or not value:
        raise ValueError("Invalid asset path")
    if "\\" in value or any(ord(ch) < 32 or ch in ':*?"<>|' for ch in value):
        raise ValueError("Unsafe asset path characters")
    path = PurePosixPath(value)
    parts = value.split("/")
    if (len(parts) < 2 or parts[0] not in ROOTS or
            str(path) != value or any(
                not part or part in (".", "..") or
                part.endswith((" ", ".")) or
                part.split(".")[0].upper() in WINDOWS_RESERVED
                for part in parts)):
        raise ValueError(f"Untrusted asset path: {value!r}")
    return path


def local_file(root, relative):
    path = safe_path(relative)
    base = Path(root).resolve(strict=True)
    target = base
    for part in path.parts:
        target = target / part
        if target.is_symlink():
            raise ValueError(f"Asset symlink rejected: {relative}")
    if not target.is_file() or not target.resolve().is_relative_to(base):
        raise ValueError(f"Missing or escaped file: {relative}")
    return target


def hash_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def present_files(root):
    base = Path(root).resolve(strict=True)
    if not base.is_dir():
        raise ValueError("Asset root is not a directory")
    files = []
    for prefix in ROOTS:
        folder = base / prefix
        if folder.is_symlink():
            raise ValueError(f"Root directory symlink rejected: {prefix}")
        if not folder.exists():
            continue
        for path in folder.rglob("*"):
            if path.is_symlink():
                raise ValueError(f"Asset symlink rejected: {path}")
            if path.is_dir():
                continue
            if not path.is_file():
                raise ValueError(f"Non-file asset rejected: {path}")
            name = path.relative_to(base).as_posix()
            safe_path(name)
            files.append(name)
            if len(files) > MAX_FILES:
                raise ValueError("Too many model/engine files")
    return sorted(files, key=str.casefold)


def inventory(root, output):
    items = []
    seen = set()
    for relative in present_files(root):
        folded = relative.casefold()
        if folded in seen:
            raise ValueError("Case-insensitive Windows path collision")
        seen.add(folded)
        file = local_file(root, relative)
        size = file.stat().st_size
        if not 0 < size <= MAX_FILE:
            raise ValueError(f"Invalid asset size: {relative}")
        items.append({
            "path": relative,
            "bytes": size,
            "sha256": hash_file(file),
            "component": "license" if relative.startswith("licenses/") else "shared",
            "source_url": "",
            "license_path": "",
            "redistribution_approved": False,
        })
    if not items:
        raise ValueError("No files found in engines/, models/ or licenses/")
    manifest = {"schema": SCHEMA, "assets": items}
    output = Path(output)
    if output.exists():
        raise ValueError("Refusing to overwrite existing manifest")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                      encoding="utf-8")
    return len(items)


def load_manifest(path):
    file = Path(path)
    if not file.is_file() or file.stat().st_size > MAX_MANIFEST:
        raise ValueError("Missing or oversized manifest")
    manifest = json.loads(file.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or manifest.get("schema") != SCHEMA:
        raise ValueError("Unsupported offline manifest schema")
    entries = manifest.get("assets")
    if not isinstance(entries, list) or not 0 < len(entries) <= MAX_FILES:
        raise ValueError("Manifest asset list invalid")
    return entries


def verify(root, manifest_path, approved=False):
    entries = load_manifest(manifest_path)
    paths, items = set(), {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise ValueError("Malformed asset item")
        name = entry.get("path")
        safe_path(name)
        folded = name.casefold()
        if folded in paths:
            raise ValueError("Duplicated/colliding Windows paths")
        paths.add(folded)
        items[name] = entry
        size = entry.get("bytes")
        expected = entry.get("sha256")
        if (type(size) is not int or size <= 0 or size > MAX_FILE or
                not isinstance(expected, str) or not HASH_RE.fullmatch(expected)):
            raise ValueError(f"Invalid SHA-256 or size in manifest: {name}")
        real = local_file(root, name)
        if real.stat().st_size != size or hash_file(real) != expected:
            raise ValueError(f"File integrity mismatch: {name}")
        if entry.get("component") not in COMPONENTS:
            raise ValueError(f"Unrecognized component: {name}")
        if approved:
            if entry.get("redistribution_approved") is not True:
                raise ValueError(f"Redistribution not approved: {name}")
            source = entry.get("source_url")
            parsed = urlsplit(source) if isinstance(source, str) else None
            if (parsed is None or parsed.scheme != "https" or
                    not parsed.hostname or parsed.username or parsed.password):
                raise ValueError(f"Source evidence URL missing: {name}")
            if not name.startswith("licenses/"):
                license_name = entry.get("license_path")
                if (not isinstance(license_name, str) or
                        not license_name.startswith("licenses/") or
                        license_name not in items and
                        license_name not in [z.get("path") for z in entries
                                             if isinstance(z, dict)]):
                    raise ValueError(f"License file missing from manifest: {name}")
    actual = {name.casefold() for name in present_files(root)}
    if paths != actual:
        raise ValueError("Unlisted or missing files under engines/, models/, licenses/")
    if approved:
        license_files = {name for name in items if name.startswith("licenses/")}
        for name, entry in items.items():
            if not name.startswith("licenses/") and entry["license_path"] not in license_files:
                raise ValueError(f"No bundled license text: {name}")
    return len(entries)


def stage(root, manifest, destination):
    verify(root, manifest, approved=True)
    output = Path(destination)
    if output.exists():
        raise ValueError("Refusing to overwrite existing staged output")
    output.parent.mkdir(parents=True, exist_ok=True)
    temp = Path(tempfile.mkdtemp(prefix=".ember-assets-", dir=output.parent))
    try:
        for entry in load_manifest(manifest):
            name = entry["path"]
            destination_file = temp.joinpath(*PurePosixPath(name).parts)
            destination_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(local_file(root, name), destination_file)
        manifest_file = temp / "offline-assets.json"
        shutil.copyfile(manifest, manifest_file)
        verify(temp, manifest_file, approved=True)
        temp.rename(output)
    finally:
        if temp.exists():
            shutil.rmtree(temp)
    return output


def self_test():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        payload = root / "source"
        for path, data in {
            "engines/llama/llama-server.exe": b"synthetic demo binary",
            "models/llm/model.gguf": b"synthetic demo model",
            "licenses/demo.txt": b"Synthetic test license; NOT a real grant",
        }.items():
            file = payload / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
        manifest = root / "inventory.json"
        assert inventory(payload, manifest) == 3
        assert verify(payload, manifest) == 3
        try:
            stage(payload, manifest, root / "unapproved")
            raise AssertionError("Unsigned redistribution was accepted")
        except ValueError:
            pass
        content = json.loads(manifest.read_text(encoding="utf-8"))
        for item in content["assets"]:
            item["source_url"] = "https://example.org/synthetic-test-evidence"
            item["redistribution_approved"] = True
            if item["path"].startswith("engines/"):
                item["component"] = "llm"
            elif item["path"].startswith("models/"):
                item["component"] = "llm"
            if not item["path"].startswith("licenses/"):
                item["license_path"] = "licenses/demo.txt"
        approved = root / "approved.json"
        approved.write_text(json.dumps(content), encoding="utf-8")
        assert verify(payload, approved, approved=True) == 3
        stage(payload, approved, root / "stage")
        assert verify(root / "stage", root / "stage" / "offline-assets.json",
                      approved=True) == 3
        (payload / "models/llm/model.gguf").write_bytes(b"modified model bytes")
        try:
            verify(payload, approved, approved=True)
            raise AssertionError("Tampered model passed checksum verification")
        except ValueError:
            pass
        for name in ("models/../data/ember-chats.sqlite", "models\\evil.gguf",
                     "/models/fake.gguf", "data/private.txt", "models/CON.txt"):
            try:
                safe_path(name)
                raise AssertionError("Unsafe path accepted")
            except ValueError:
                pass
        extra = payload / "models" / "not-in-manifest.bin"
        extra.write_bytes(b"x")
        try:
            verify(payload, approved)
            raise AssertionError("Unlisted executable or model asset accepted")
        except ValueError:
            pass
        if hasattr(os, "symlink"):
            link = payload / "engines" / "llama" / "link.exe"
            try:
                os.symlink(payload / "engines/llama/llama-server.exe", link)
            except OSError:
                pass
            else:
                try:
                    present_files(payload)
                    raise AssertionError("Symlink accepted")
                except ValueError:
                    pass
    print("offline_bundle self-test passed (checksums, no overwrites, unsafe paths, licenses)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("self-test")
    for name in ("inventory", "verify", "stage"):
        command = actions.add_parser(name)
        command.add_argument("--root", required=True, type=Path)
        command.add_argument("--manifest", required=True, type=Path)
        if name == "stage":
            command.add_argument("--output", required=True, type=Path)
        if name == "verify":
            command.add_argument("--redistribution-required", action="store_true")
    args = parser.parse_args()
    try:
        if args.action == "self-test":
            self_test()
        elif args.action == "inventory":
            print(f"Inventory created for {inventory(args.root, args.manifest)} local files")
        elif args.action == "verify":
            print(f"Verified {verify(args.root, args.manifest, args.redistribution_required)} local files")
        elif args.action == "stage":
            print(f"Fully offline asset directory staged at {stage(args.root, args.manifest, args.output)}")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.error(f"{type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
