#!/usr/bin/env python3
"""Manually split/join complete offline Ember ZIPs for <2-GiB GitHub assets.

No network, API calls, uploads, model launches, automation, scheduled tasks or
third-party dependencies. The normal whole ZIP is always the primary output;
these checked parts are only an additional fallback distribution format.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

SCHEMA = "ember-large-release-v1"
PART_BYTES_DEFAULT = 2_000_000_000   # below GitHub's 2-GiB per-release-asset limit
PART_BYTES_MAX = 2_000_000_000
MAX_PARTS = 1000
MAX_MANIFEST = 512 * 1024
MAX_NAME = 180
BLOCK = 1024 * 1024
HASH = re.compile(r"[0-9a-f]{64}\Z")
NAME = re.compile(r"[A-Za-z0-9][A-Za-z0-9._-]{1,179}\.zip\Z")


def linked(path: Path) -> bool:
    return path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction())


def validate_filename(name: str) -> str:
    if (not isinstance(name, str) or len(name) > MAX_NAME or
            not NAME.fullmatch(name) or name.startswith(".") or
            ".." in name or name.endswith(".part")):
        raise ValueError("Unsafe or invalid complete archive filename")
    return name


def manifest_data(parts_dir: Path, manifest: Path | None = None) -> dict:
    if linked(parts_dir) or not parts_dir.is_dir():
        raise ValueError("Parts directory must be real and present")
    if manifest is None:
        candidates = list(parts_dir.glob("*.parts.json"))
        if len(candidates) != 1:
            raise ValueError("Exactly one .parts.json required")
        manifest = candidates[0]
    if linked(manifest) or not manifest.is_file() or manifest.parent.resolve() != parts_dir.resolve():
        raise ValueError("Manifest must be a regular file inside its parts directory")
    if manifest.stat().st_size > MAX_MANIFEST:
        raise ValueError("Large or corrupt parts manifest")
    obj = json.loads(manifest.read_text(encoding="utf-8"))
    if not isinstance(obj, dict) or obj.get("schema") != SCHEMA:
        raise ValueError("Unsupported split manifest schema")
    filename = validate_filename(obj.get("archive"))
    total = obj.get("bytes")
    digest = obj.get("sha256")
    entries = obj.get("parts")
    if (type(total) is not int or total < 1 or not isinstance(digest, str) or
            not HASH.fullmatch(digest) or not isinstance(entries, list) or
            not 1 <= len(entries) <= MAX_PARTS):
        raise ValueError("Invalid complete archive metadata")
    expected_names = []
    total_parts = 0
    for index, item in enumerate(entries, start=1):
        name = f"{filename}.part{index:04d}"
        expected_names.append(name)
        if (not isinstance(item, dict) or item.get("name") != name or
                type(item.get("bytes")) is not int or
                not 0 < item["bytes"] <= PART_BYTES_MAX or
                not isinstance(item.get("sha256"), str) or
                not HASH.fullmatch(item["sha256"])):
            raise ValueError("Bad or reordered split slice")
        total_parts += item["bytes"]
        file = parts_dir / name
        if linked(file) or not file.is_file() or file.stat().st_size != item["bytes"]:
            raise ValueError("Missing, linked, or incorrect-size release part")
    if total_parts != total:
        raise ValueError("Total release archive length does not match its parts")
    expected_names.append(manifest.name)
    all_names = {p.name for p in parts_dir.iterdir()}
    if all_names != set(expected_names):
        raise ValueError("Unexpected extra or missing files in the release-parts directory")
    return obj


def split(archive: Path, output: Path, part_bytes: int = PART_BYTES_DEFAULT) -> dict:
    if (type(part_bytes) is not int or not 1 <= part_bytes <= PART_BYTES_MAX):
        raise ValueError("Part size must be positive and strictly below the release asset limit")
    if linked(archive) or not archive.is_file():
        raise ValueError("Complete source ZIP must be an existing regular local file")
    name = validate_filename(archive.name)
    if archive.stat().st_size < 1 or archive.stat().st_size > part_bytes * MAX_PARTS:
        raise ValueError("Archive is empty or requires too many release parts")
    if linked(output.parent) or not output.parent.is_dir() or output.exists() or linked(output):
        raise ValueError("Output must be a NEW directory under an existing real parent")
    temporary = Path(tempfile.mkdtemp(prefix=".ember-partstage-", dir=output.parent))
    try:
        total_digest = hashlib.sha256()
        slices = []
        with archive.open("rb") as source:
            part = 0
            while True:
                if part >= MAX_PARTS:
                    raise ValueError("Exceeded maximum number of release parts")
                first = source.read(min(BLOCK, part_bytes))
                if not first:
                    break
                part += 1
                fname = f"{name}.part{part:04d}"
                path = temporary / fname
                part_digest = hashlib.sha256()
                consumed = 0
                with path.open("xb") as dest:
                    block = first
                    while block:
                        dest.write(block)
                        consumed += len(block)
                        total_digest.update(block)
                        part_digest.update(block)
                        remaining = part_bytes - consumed
                        if remaining <= 0:
                            break
                        block = source.read(min(BLOCK, remaining))
                slices.append({"name": fname, "bytes": consumed,
                               "sha256": part_digest.hexdigest()})
        manifest = {"schema": SCHEMA, "archive": name,
                    "bytes": sum(p["bytes"] for p in slices),
                    "sha256": total_digest.hexdigest(), "parts": slices}
        (temporary / f"{name}.parts.json").write_text(
            json.dumps(manifest, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        if not slices or manifest["bytes"] != archive.stat().st_size:
            raise ValueError("Source archive changed during split")
        manifest_data(temporary)  # Filesystem completeness/size checks
        for item in slices:
            # Verify actual bytes on disk after writing, not just the hashes
            # observed as source blocks were streamed into the destination.
            saved_digest = hashlib.sha256()
            with (temporary / item["name"]).open("rb") as saved:
                for block in iter(lambda: saved.read(BLOCK), b""):
                    saved_digest.update(block)
            if saved_digest.hexdigest() != item["sha256"]:
                raise ValueError("Freshly staged part differs from source data")
        if output.exists() or linked(output):
            raise ValueError("Refusing concurrent replacement of output directory")
        temporary.rename(output)
        return manifest
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def join(parts_dir: Path, output: Path, manifest: Path | None = None) -> str:
    data = manifest_data(parts_dir, manifest)
    output = Path(output)
    if output.name != data["archive"] or output.exists() or linked(output):
        raise ValueError("Reconstructed output must use original name and must not already exist")
    if not output.parent.is_dir() or linked(output.parent):
        raise ValueError("Output folder missing or linked")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=".ember-join-", suffix=".zip",
                                         dir=output.parent, delete=False) as handle:
            temporary = Path(handle.name)
        full_digest = hashlib.sha256()
        total = 0
        with temporary.open("wb") as dest:
            for item in data["parts"]:
                part_digest = hashlib.sha256()
                read = 0
                part = parts_dir / item["name"]
                with part.open("rb") as source:
                    for block in iter(lambda: source.read(BLOCK), b""):
                        read += len(block)
                        total += len(block)
                        part_digest.update(block)
                        full_digest.update(block)
                        dest.write(block)
                if read != item["bytes"] or part_digest.hexdigest() != item["sha256"]:
                    raise ValueError("Release part content does not match its manifest")
            dest.flush()
            os.fsync(dest.fileno())
        if total != data["bytes"] or full_digest.hexdigest() != data["sha256"]:
            raise ValueError("Full reconstructed ZIP differs from manifest")
        if output.exists() or linked(output):
            raise ValueError("Reconstructed output appeared concurrently")
        os.replace(temporary, output)
        temporary = None
        return full_digest.hexdigest()
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="ember-release-parts-") as temp:
        root = Path(temp)
        original = root / "Ember-0.1.0-dev-Windows-x64-portable.zip"
        body = b"PK\x03\x04" + bytes(range(256)) * 9 + b"last-17-bytes...!"
        original.write_bytes(body)
        parts = root / "parts"
        meta = split(original, parts, part_bytes=511)
        assert meta["bytes"] == len(body) and len(meta["parts"]) > 3
        assert all(x["bytes"] <= 511 for x in meta["parts"])
        outdir = root / "restored"
        outdir.mkdir()
        restored = outdir / original.name
        assert join(parts, restored) == hashlib.sha256(body).hexdigest()
        assert restored.read_bytes() == body
        try:
            join(parts, restored)
            raise AssertionError("Overwriting a real output was allowed")
        except ValueError:
            pass
        corrupt = parts / meta["parts"][0]["name"]
        corrupt.write_bytes(b"!" + corrupt.read_bytes()[1:])
        tamper_dir = root / "restore-tamper"
        tamper_dir.mkdir()
        try:
            join(parts, tamper_dir / original.name)
            raise AssertionError("Tampered part was accepted")
        except ValueError:
            pass
        corrupt.write_bytes(body[:511])
        last = parts / meta["parts"][-1]["name"]
        saved = last.read_bytes()
        last.unlink()
        missing_dir = root / "restore-missing"
        missing_dir.mkdir()
        try:
            join(parts, missing_dir / original.name)
            raise AssertionError("Missing part was accepted")
        except ValueError:
            pass
        last.write_bytes(saved)
        extra = parts / "unexpected.exe"
        extra.write_bytes(b"x")
        extra_dir = root / "restore-extra"
        extra_dir.mkdir()
        try:
            join(parts, extra_dir / original.name)
            raise AssertionError("Unapproved extra file was accepted")
        except ValueError:
            pass
        extra.unlink()
        try:
            split(original, parts, part_bytes=511)
            raise AssertionError("Split overwrote an existing directory")
        except ValueError:
            pass
        try:
            split(original, root / "bad", part_bytes=PART_BYTES_MAX + 1)
            raise AssertionError("Oversized release slice was accepted")
        except ValueError:
            pass
    print("large_release_asset: split/join, tamper, missing, extra and overwrite tests passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("self-test")
    split_parser = actions.add_parser("split")
    split_parser.add_argument("--source", type=Path, required=True)
    split_parser.add_argument("--output", type=Path, required=True,
                              help="NEW output directory for release parts and manifest")
    split_parser.add_argument("--part-bytes", type=int, default=PART_BYTES_DEFAULT)
    join_parser = actions.add_parser("join")
    join_parser.add_argument("--parts", type=Path, required=True)
    join_parser.add_argument("--output", type=Path, required=True,
                             help="NEW file named exactly like the original complete archive")
    args = parser.parse_args()
    try:
        if args.action == "self-test":
            self_test()
        elif args.action == "split":
            res = split(args.source, args.output, args.part_bytes)
            print(f"Release parts created: {len(res['parts'])} pieces; full SHA256 {res['sha256']}")
        else:
            digest = join(args.parts, args.output)
            print(f"Complete ZIP restored byte-for-byte; SHA256 {digest}")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        parser.error(f"{type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
