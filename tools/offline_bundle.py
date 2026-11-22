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
MIN_MODEL = 1024 * 1024
# These are the exact paths expected by Ember's native, download-free launch
# defaults. A manifest with arbitrary "llm"/"asr" labels is not sufficient.
REQUIRED_TEXT_FIRST = {
    "engines/llama/llama-server.exe": "llm",
    "models/llm/model.gguf": "llm",
    "engines/whisper/whisper-cli.exe": "asr",
    "models/stt/ggml-base.bin": "asr",
}
HASH_RE = re.compile(r"^[0-9a-f]{64}$")
WINDOWS_RESERVED = {"CON", "PRN", "AUX", "NUL"} | {
    f"{letter}{number}" for letter in ("COM", "LPT") for number in range(1, 10)
}
# Installer and ZIP64 payloads must never contain previously recorded chats,
# microphone scratch, local credentials, tokens or development secrets.
PRIVATE_PARTS = {"data", "cache", ".git", ".ssh", ".aws", ".env",
                 "qml-smoke-report.txt", "ember.ini", "credentials.json",
                 "id_rsa", "id_ed25519"}
PRIVATE_SUFFIXES = (".sqlite", ".sqlite3", ".pem", ".key", ".p12", ".pfx")


def is_link_or_junction(path):
    return path.is_symlink() or (
        hasattr(path, "is_junction") and path.is_junction())


def private_payload_path(relative):
    parts = [part.casefold() for part in PurePosixPath(relative).parts]
    return any(part in PRIVATE_PARTS or part.startswith(".env.") or
               part.endswith(PRIVATE_SUFFIXES) for part in parts)


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
        if is_link_or_junction(target):
            raise ValueError(f"Asset reparse-point link rejected: {relative}")
    if not target.is_file() or not target.resolve().is_relative_to(base):
        raise ValueError(f"Missing or escaped file: {relative}")
    return target


def hash_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_windows_amd64_pe(path, name):
    """Non-executing header sanity check; cannot prove runtime DLL completeness."""
    if path.stat().st_size < 256:
        raise ValueError(f"Windows executable is too short: {name}")
    with path.open("rb") as stream:
        dos = stream.read(64)
        if len(dos) < 64 or dos[:2] != b"MZ":
            raise ValueError(f"Not a Windows PE executable: {name}")
        offset = int.from_bytes(dos[60:64], "little")
        if not 64 <= offset <= 4096 or offset + 6 > path.stat().st_size:
            raise ValueError(f"Invalid Windows PE offset: {name}")
        stream.seek(offset)
        if stream.read(6) != b"PE\x00\x00\x64\x86":
            raise ValueError(f"Windows executable is not AMD64 PE: {name}")


def verify_gguf_header(path, name):
    if not name.lower().endswith(".gguf") or path.stat().st_size < MIN_MODEL:
        raise ValueError(f"Expected substantial GGUF model: {name}")
    with path.open("rb") as stream:
        header = stream.read(8)
    if header[:4] != b"GGUF" or len(header) != 8 or (
            int.from_bytes(header[4:], "little") not in (2, 3)):
        raise ValueError(f"Invalid GGUF v2/v3 header: {name}")


def verify_multilingual_whisper_header(path, name):
    if (not name.lower().endswith(".bin") or
            name.lower().endswith(".en.bin") or
            path.stat().st_size < MIN_MODEL):
        raise ValueError(f"Expected substantial multilingual Whisper GGML .bin: {name}")
    with path.open("rb") as stream:
        magic = stream.read(4)
    if magic != b"lmgg":
        raise ValueError(f"Invalid whisper.cpp GGML model header: {name}")


def verify_text_first_shapes(root, items):
    """Mandatory default-path and file-header gate for approved bundles only.

    Header checks do NOT establish that weights are complete, licensed,
    multilingual, compatible with a selected engine, or runnable on a PC.
    Actual subprocess inference/ASR acceptance is a separate manual gate.
    """
    for name, component in REQUIRED_TEXT_FIRST.items():
        if name not in items or items[name].get("component") != component:
            raise ValueError(f"Required Ember default asset/component missing: {name}")
    for name in items:
        path = local_file(root, name)
        lowered = name.lower()
        if name.startswith("engines/") and lowered.endswith((".exe", ".dll")):
            verify_windows_amd64_pe(path, name)
        if name.startswith("models/") and lowered.endswith(".gguf"):
            verify_gguf_header(path, name)
    verify_multilingual_whisper_header(
        local_file(root, "models/stt/ggml-base.bin"), "models/stt/ggml-base.bin")


def present_files(root):
    base = Path(root).resolve(strict=True)
    if not base.is_dir():
        raise ValueError("Asset root is not a directory")
    files = []
    for prefix in ROOTS:
        folder = base / prefix
        if is_link_or_junction(folder):
            raise ValueError(f"Root directory link/junction rejected: {prefix}")
        if not folder.exists():
            continue
        for path in folder.rglob("*"):
            if is_link_or_junction(path):
                raise ValueError(f"Asset symlink/junction rejected: {path}")
            if path.is_dir():
                continue
            if not path.is_file():
                raise ValueError(f"Non-file asset rejected: {path}")
            name = path.relative_to(base).as_posix()
            safe_path(name)
            if private_payload_path(name):
                raise ValueError("Private or secret file inside offline asset payload")
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
        # Text-first milestone requires both a local LLM and RU/EN ASR;
        # natural TTS is optional and must NOT block that release profile.
        # Voice-capable bundles may include TTS with independently approved
        # executable/model/license evidence, but cannot omit its runtime.
        components = {"llm", "asr"}
        available_models = {
            entry["component"] for name, entry in items.items()
            if name.startswith("models/")
        }
        if not components.issubset(available_models):
            raise ValueError("Text-first offline package requires LLM and ASR model weights")
        engine_components = {
            entry["component"] for name, entry in items.items()
            if name.startswith("engines/")
        }
        if "tts" in available_models:
            components.add("tts")
        for component in components:
            if component not in engine_components and "shared" not in engine_components:
                raise ValueError(f"Required {component} engine executable missing")
        verify_text_first_shapes(root, items)
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


def assemble(staged_root, manifest, application, output):
    """Combine clean windeployqt output and approved offline assets without user data."""
    verify(staged_root, manifest, approved=True)
    app = Path(application).resolve(strict=True)
    for required in ("Ember.exe", "Qt6Core.dll", "LICENSE"):
        if not (app / required).is_file():
            raise ValueError(f"Missing clean native app dependency: {required}")
    forbidden = {"data", "cache", "engines", "models", "licenses",
                 "offline-assets.json", "qml-smoke-report.txt", ".env", ".git"}
    for candidate in app.rglob("*"):
        if is_link_or_junction(candidate):
            raise ValueError(f"Application link/junction rejected: {candidate}")
        relative = candidate.relative_to(app)
        if relative.parts[0].casefold() in forbidden or private_payload_path(relative.as_posix()):
            raise ValueError("Deployed Qt app contains private data, secrets or untracked models")
        if not (candidate.is_file() or candidate.is_dir()):
            raise ValueError("Unexpected native application file type")
    out = Path(output)
    if out.exists():
        raise ValueError("Refusing to overwrite existing assembled directory")
    out.parent.mkdir(parents=True, exist_ok=True)
    temp = Path(tempfile.mkdtemp(prefix=".ember-complete-", dir=out.parent))
    try:
        shutil.copytree(app, temp, dirs_exist_ok=True)
        for entry in load_manifest(manifest):
            name = entry["path"]
            dest = temp.joinpath(*PurePosixPath(name).parts)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(local_file(staged_root, name), dest)
        shutil.copyfile(manifest, temp / "offline-assets.json")
        verify(temp, temp / "offline-assets.json", approved=True)
        temp.rename(out)
    finally:
        if temp.exists():
            shutil.rmtree(temp)
    return out


def fake_pe64():
    # Minimal synthetic header for a non-executing preflight, not runnable.
    data = bytearray(256)
    data[:2] = b"MZ"
    data[60:64] = (128).to_bytes(4, "little")
    data[128:134] = b"PE\x00\x00\x64\x86"
    return bytes(data)


def fake_gguf():
    # At least 1 MiB, GGUF v3 header; synthetic zero bytes are NOT model data.
    return b"GGUF" + (3).to_bytes(4, "little") + bytes(MIN_MODEL)


def fake_whisper():
    # Legacy whisper.cpp GGML magic and synthetic zero bytes, NOT real ASR.
    return b"lmgg" + bytes(MIN_MODEL)


def self_test():
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        payload = root / "source"
        for path, data in {
            "engines/llama/llama-server.exe": fake_pe64(),
            "models/llm/model.gguf": fake_gguf(),
            "engines/whisper/whisper-cli.exe": fake_pe64(),
            "models/stt/ggml-base.bin": fake_whisper(),
            "engines/speech/speech.exe": fake_pe64(),
            "models/speech/qwen3-tts-0.6b.gguf": fake_gguf(),
            "licenses/demo.txt": b"Synthetic test license; NOT a real grant",
        }.items():
            file = payload / path
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_bytes(data)
        manifest = root / "inventory.json"
        assert inventory(payload, manifest) == 7
        assert verify(payload, manifest) == 7
        try:
            stage(payload, manifest, root / "unapproved")
            raise AssertionError("Unsigned redistribution was accepted")
        except ValueError:
            pass
        content = json.loads(manifest.read_text(encoding="utf-8"))
        for item in content["assets"]:
            item["source_url"] = "https://example.org/synthetic-test-evidence"
            item["redistribution_approved"] = True
            if "/llama/" in item["path"] or "/llm/" in item["path"]:
                item["component"] = "llm"
            elif "/whisper/" in item["path"] or "/stt/" in item["path"]:
                item["component"] = "asr"
            elif "/speech/" in item["path"]:
                item["component"] = "tts"
            if not item["path"].startswith("licenses/"):
                item["license_path"] = "licenses/demo.txt"
        approved = root / "approved.json"
        approved.write_text(json.dumps(content), encoding="utf-8")
        assert verify(payload, approved, approved=True) == 7

        # Approval must reject a forged but correctly rehashed payload, not
        # merely catch different bytes against an old SHA-256 manifest.
        def reject_rehashed(relative, altered):
            source = payload / relative
            before = source.read_bytes()
            source.write_bytes(altered)
            forged = json.loads(json.dumps(content))
            entry = next(x for x in forged["assets"] if x["path"] == relative)
            entry["bytes"] = len(altered)
            entry["sha256"] = hashlib.sha256(altered).hexdigest()
            candidate = root / "forged-headers.json"
            candidate.write_text(json.dumps(forged), encoding="utf-8")
            try:
                verify(payload, candidate, approved=True)
                raise AssertionError(f"Forged approved file passed: {relative}")
            except ValueError:
                pass
            finally:
                source.write_bytes(before)

        reject_rehashed("models/llm/model.gguf",
                        b"GGUF" + (99).to_bytes(4, "little") + bytes(MIN_MODEL))
        reject_rehashed("models/stt/ggml-base.bin",
                        b"WRONG" + bytes(MIN_MODEL))
        reject_rehashed("engines/llama/llama-server.exe",
                        fake_pe64()[:132] + b"\x4c\x01" + fake_pe64()[134:])
        # A checked and correctly hashed x86 PE is still not an x64 runtime.

        # Reject bundles that advertise the right component labels but don't
        # actually contain Ember's default runtime paths.
        renamed = root / "renamed-default"
        shutil.copytree(payload, renamed)
        wrong = json.loads(json.dumps(content))
        old = renamed / "engines/llama/llama-server.exe"
        alt = renamed / "engines/llama/alternative.exe"
        old.rename(alt)
        next(x for x in wrong["assets"]
             if x["path"] == "engines/llama/llama-server.exe")["path"] = (
                 "engines/llama/alternative.exe")
        wrong_manifest = root / "wrong-default.json"
        wrong_manifest.write_text(json.dumps(wrong), encoding="utf-8")
        try:
            verify(renamed, wrong_manifest, approved=True)
            raise AssertionError("Renamed mandatory runtime passed approval")
        except ValueError:
            pass

        # Text-first approval must succeed without any TTS weights or engine;
        # otherwise an optional voice milestone silently blocks delivery.
        text_payload = root / "text-only-source"
        for item in content["assets"]:
            if item["component"] == "tts":
                continue
            src = payload / item["path"]
            dest = text_payload / item["path"]
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(src.read_bytes())
        text_doc = {"schema": content["schema"], "assets": [
            item for item in content["assets"] if item["component"] != "tts"
        ]}
        text_manifest = root / "text-only-approved.json"
        text_manifest.write_text(json.dumps(text_doc), encoding="utf-8")
        assert verify(text_payload, text_manifest, approved=True) == 5
        stage(text_payload, text_manifest, root / "text-only-stage")
        assert verify(root / "text-only-stage",
                      root / "text-only-stage" / "offline-assets.json",
                      approved=True) == 5
        # A TTS model without its runtime is not an accepted voice bundle.
        no_tts_engine = root / "tts-without-engine"
        partial = [item for item in content["assets"]
                   if item["path"] != "engines/speech/speech.exe"]
        for item in partial:
            src = payload / item["path"]
            dest = no_tts_engine / item["path"]
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(src.read_bytes())
        bad_manifest = root / "tts-without-engine.json"
        bad_manifest.write_text(json.dumps({"schema": content["schema"],
                                            "assets": partial}), encoding="utf-8")
        try:
            verify(no_tts_engine, bad_manifest, approved=True)
            raise AssertionError("TTS model without approved runtime passed")
        except ValueError:
            pass
        stage(payload, approved, root / "stage")
        assert verify(root / "stage", root / "stage" / "offline-assets.json",
                      approved=True) == 7
        app = root / "qt-deployed"
        app.mkdir()
        for name in ("Ember.exe", "Qt6Core.dll", "LICENSE"):
            (app / name).write_bytes(b"synthetic native test payload")
        assembled = assemble(root / "stage",
                             root / "stage" / "offline-assets.json",
                             app, root / "complete")
        assert (assembled / "Ember.exe").is_file()
        assert verify(assembled, assembled / "offline-assets.json", approved=True) == 7
        # A stray private file anywhere in the clean application tree is
        # forbidden even if its directory is not named data/ or cache/.
        for relative in ("nested/credentials.json", "logs/session.pem",
                         "qml-smoke-report.txt", ".env"):
            secret = app / relative
            secret.parent.mkdir(parents=True, exist_ok=True)
            secret.write_bytes(b"PRIVATE")
            try:
                assemble(root / "stage", root / "stage" / "offline-assets.json",
                         app, root / ("reject-secret-" +
                                      relative.replace("/", "-")))
                raise AssertionError("Deployed private file passed: " + relative)
            except ValueError:
                pass
            secret.unlink()
            if secret.parent != app:
                secret.parent.rmdir()
        (app / "data").mkdir()
        (app / "data" / "ember-chats.sqlite").write_bytes(b"SECRET")
        try:
            assemble(root / "stage", root / "stage" / "offline-assets.json",
                     app, root / "must-refuse")
            raise AssertionError("Private chat data was accepted in distributable app")
        except ValueError:
            pass
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
    # The public placeholder manifest must enumerate every required runtime.
    # A new release template must never regress to an LLM-only package.
    template = Path(__file__).resolve().parent.parent / "release/offline-assets.template.json"
    skeleton = {entry["path"]: entry for entry in load_manifest(template)}
    for required, component in REQUIRED_TEXT_FIRST.items():
        assert required in skeleton and skeleton[required]["component"] == component
        assert skeleton[required]["redistribution_approved"] is False
    # The manually invoked Windows acceptance tester has a pure offline
    # self-test: test the parser and private WAV boundaries without starting
    # llama-server, whisper-cli or any scheduled task.
    from offline_acceptance import self_test as acceptance_self_test
    acceptance_self_test()
    print("offline_bundle self-test passed (checksums, headers, default paths, no overwrites, unsafe paths, licenses)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("self-test")
    for name in ("inventory", "verify", "stage", "assemble"):
        command = actions.add_parser(name)
        command.add_argument("--root", required=True, type=Path)
        command.add_argument("--manifest", required=True, type=Path)
        if name in ("stage", "assemble"):
            command.add_argument("--output", required=True, type=Path)
        if name == "assemble":
            command.add_argument("--application", required=True, type=Path)
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
        elif args.action == "assemble":
            print(f"Complete offline native payload assembled at {assemble(args.root, args.manifest, args.application, args.output)}")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.error(f"{type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
