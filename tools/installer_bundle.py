#!/usr/bin/env python3
"""One-shot ZIP64 package of a complete Inno Setup disk-spanned installer.

The resulting single ZIP holds Setup.exe and EVERY contiguous Setup-N.bin
slice needed for a fully offline installation. No network, subprocesses,
model loading, scheduling, secrets, or automatic publishing.
"""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import tempfile
import zipfile

VERSION = re.compile(r"(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)(?:-dev)?\Z")
MAX_SLICES = 128
MAX_SLICE_BYTES = 8 * 1024 ** 3
BLOCK = 1024 * 1024


def is_link(path: Path) -> bool:
    return path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction())


def select_installer_files(directory: Path, version: str) -> list[Path]:
    if not VERSION.fullmatch(version):
        raise ValueError("Version must be an exact SemVer release or -dev candidate")
    if is_link(directory) or not directory.is_dir():
        raise ValueError("Installer source must be an existing real local directory")
    base = f"Ember-{version}-Windows-x64"
    executable = directory / f"{base}.exe"
    pattern = re.compile(re.escape(base) + r"-([1-9][0-9]*)\.bin\Z", re.IGNORECASE)
    if is_link(executable) or not executable.is_file():
        raise ValueError("The compiled Inno Setup EXE is missing or linked")
    if executable.stat().st_size < 1:
        raise ValueError("The setup executable cannot be empty")
    if executable.stat().st_size > MAX_SLICE_BYTES:
        raise ValueError("Setup executable is implausibly large")
    parts = {}
    for item in directory.iterdir():
        if not item.name.casefold().startswith(base.casefold() + "-"):
            continue
        # Other complete distributions have names beginning with the same
        # prefix. They are intentionally NOT included in this installer ZIP.
        if item.suffix.casefold() not in (".bin", ".exe"):
            continue
        match = pattern.fullmatch(item.name)
        if match is None:
            raise ValueError("Unrecognized matching setup binary/slice filename")
        index = int(match.group(1))
        if index in parts or index > MAX_SLICES:
            raise ValueError("Duplicate or excessive installer slice number")
        if is_link(item) or not item.is_file() or not 0 < item.stat().st_size <= MAX_SLICE_BYTES:
            raise ValueError("Missing, linked or invalid setup slice")
        parts[index] = item
    if not parts or sorted(parts) != list(range(1, len(parts) + 1)):
        raise ValueError("Disk-spanned installer requires complete contiguous slices 1..N")
    return [executable, *(parts[n] for n in sorted(parts))]


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(BLOCK), b""):
            digest.update(block)
    return digest.hexdigest()


def package(directory: Path, version: str, destination: Path) -> dict:
    files = select_installer_files(directory, version)
    destination = Path(destination)
    if destination.exists() or is_link(destination):
        raise ValueError("Installer bundle output already exists: refusing overwrite")
    if not destination.parent.is_dir() or is_link(destination.parent):
        raise ValueError("Output parent must be an existing, real local directory")
    if destination.suffix.lower() != ".zip":
        raise ValueError("Complete installer distribution must be a ZIP")
    # All entries are direct children, never paths from untrusted sources.
    if destination.name in {p.name for p in files}:
        raise ValueError("Bundle output may not collide with setup input")
    temporary = None
    hashes = {}
    try:
        with tempfile.NamedTemporaryFile(prefix=".ember-install-", suffix=".zip",
                                         dir=destination.parent, delete=False) as handle:
            temporary = Path(handle.name)
        with zipfile.ZipFile(temporary, mode="w", allowZip64=True) as archive:
            for source in files:
                size = source.stat().st_size
                info = zipfile.ZipInfo(source.name, date_time=(2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_STORED
                info.external_attr = 0o100644 << 16
                info.file_size = size
                digest = hashlib.sha256()
                count = 0
                with source.open("rb") as reader, archive.open(
                        info, mode="w", force_zip64=True) as writer:
                    for block in iter(lambda: reader.read(BLOCK), b""):
                        count += len(block)
                        digest.update(block)
                        writer.write(block)
                if count != size:
                    raise ValueError("Setup file changed size during archive assembly")
                hashes[source.name] = digest.hexdigest()
        # Check that ZIP64 member content round-trips byte-for-byte. A
        # central directory check alone would miss truncated/corrupt slices.
        with zipfile.ZipFile(temporary, mode="r", allowZip64=True) as archive:
            if archive.namelist() != [p.name for p in files]:
                raise ValueError("Installer bundle lost or reordered a slice")
            for source in files:
                info = archive.getinfo(source.name)
                if info.compress_type != zipfile.ZIP_STORED:
                    raise ValueError("Installer slices were unexpectedly recompressed")
                if info.file_size != source.stat().st_size:
                    raise ValueError("ZIP64 member size mismatch")
                digest = hashlib.sha256()
                with archive.open(source.name) as reader:
                    for block in iter(lambda: reader.read(BLOCK), b""):
                        digest.update(block)
                if digest.hexdigest() != hashes[source.name]:
                    raise ValueError("Installer ZIP member failed complete SHA-256 replay")
        zip_hash = file_sha256(temporary)
        if destination.exists() or is_link(destination):
            raise ValueError("Refusing concurrent ZIP output replacement")
        os.replace(temporary, destination)
        temporary = None
        return {"zip_sha256": zip_hash, "installer_exe": files[0].name,
                "slices": len(files) - 1, "members": len(files)}
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def self_test() -> None:
    with tempfile.TemporaryDirectory(prefix="ember-installer-zip-test-") as temp:
        root = Path(temp)
        base = "Ember-0.1.0-dev-Windows-x64"
        exe = root / (base + ".exe")
        one = root / (base + "-1.bin")
        two = root / (base + "-2.bin")
        exe.write_bytes(b"MZ" + b"stub setup" * 32)
        one.write_bytes(b"inno asset model" * 172)
        two.write_bytes(b"native voice content" * 121)
        assert len(select_installer_files(root, "0.1.0-dev")) == 3
        archive_path = root / (base + "-installer-bundle.zip")
        report = package(root, "0.1.0-dev", archive_path)
        assert report["slices"] == 2 and len(report["zip_sha256"]) == 64
        with zipfile.ZipFile(archive_path) as archive:
            assert archive.read(one.name) == one.read_bytes()
            assert archive.read(two.name) == two.read_bytes()
            assert all(z.compress_type == zipfile.ZIP_STORED for z in archive.infolist())
        try:
            package(root, "0.1.0-dev", archive_path)
            raise AssertionError("Refusing to overwrite destination failed")
        except ValueError:
            pass
        one.unlink()
        try:
            select_installer_files(root, "0.1.0-dev")
            raise AssertionError("Gap in required setup slices was accepted")
        except ValueError:
            pass
        one.write_bytes(b"first")
        bad = root / (base + "-3.exe")
        bad.write_bytes(b"unknown binary")
        try:
            select_installer_files(root, "0.1.0-dev")
            raise AssertionError("Unknown setup binary was silently omitted")
        except ValueError:
            pass
        bad.unlink()
        if hasattr(os, "symlink"):
            one.unlink()
            try:
                os.symlink(two, one)
            except (OSError, NotImplementedError):
                pass
            else:
                try:
                    select_installer_files(root, "0.1.0-dev")
                    raise AssertionError("Linked setup slice was accepted")
                except ValueError:
                    pass
        for bad_version in ("../0.1.0", "1.0.0-beta", "1.2.3/../../x", "1.2"):
            try:
                select_installer_files(root, bad_version)
                raise AssertionError("Unsafe version was accepted")
            except ValueError:
                pass
    print("installer_bundle: 9 synthetic ZIP64 slice/integrity checks passed")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--directory", type=Path, help="Directory holding compiled Inno Setup.exe and numbered .bin slices")
    parser.add_argument("--version", help="SemVer version, optionally -dev")
    parser.add_argument("--output", type=Path, help="New single download ZIP path")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.directory is None or args.version is None or args.output is None:
        parser.error("--directory, --version and --output required")
    try:
        result = package(args.directory, args.version, args.output)
    except (ValueError, OSError, zipfile.BadZipFile, zipfile.LargeZipFile) as exc:
        parser.error(f"{type(exc).__name__}: {exc}")
    print(f"Full offline installer ZIP validated: {result['members']} files, "
          f"{result['slices']} complete Inno data slices, "
          f"ZIP SHA-256 {result['zip_sha256']}")


if __name__ == "__main__":
    main()
