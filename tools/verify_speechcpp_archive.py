#!/usr/bin/env python3
"""Offline-only verification/staging of a PINNED official Windows speech.cpp ZIP.

A maintainer downloads the official release elsewhere, or brings it by USB.
This tool never contacts the network, executes binaries, or downloads weights.
It does NOT assert that any model runs, nor approve bundled dependency licenses.
"""
import argparse
import hashlib
import io
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import tempfile
import zipfile

VERSION = "0.11.2"
ARCHIVE_NAME = f"speech-{VERSION}-windows-x64-vulkan.zip"
ARCHIVE_SIZE = 29726411
# GitHub release v0.11.2 asset digest, not guessed from filename:
ARCHIVE_SHA256 = "2cf46f0351a2a90b9ce0bfb83cf63ac209ca31c88df9a5bc0126898a20a58586"
UPSTREAM = f"https://github.com/nyosegawa/speech.cpp/releases/tag/v{VERSION}"
MAX_FILES = 150
MAX_MEMBER = 256 * 1024 * 1024
MAX_TOTAL_UNPACKED = 512 * 1024 * 1024
WINDOWS_RESERVED = {"CON", "PRN", "AUX", "NUL"} | {
    f"{name}{number}" for name in ("COM", "LPT") for number in range(1, 10)
}


def linked(path):
    return path.is_symlink() or (
        hasattr(path, "is_junction") and path.is_junction()
    ) or (
        os.name == "nt" and path.exists() and
        bool(path.lstat().st_file_attributes & 1024)
    )


def safe_member(name):
    if not isinstance(name, str) or not 1 <= len(name) <= 240:
        raise ValueError("Invalid ZIP member name")
    if ("\\" in name or ":" in name or name.startswith("/") or
            any(ord(c) < 32 or c in '*?"<>|' for c in name)):
        raise ValueError("Unsafe Windows archive path")
    stripped = name[:-1] if name.endswith("/") else name
    parts = stripped.split("/")
    if any(not p or p in (".", "..") or p[-1] in (" ", ".") or
           p.split(".")[0].upper() in WINDOWS_RESERVED for p in parts):
        raise ValueError("Windows traversal/reserved/trailing name")
    result = PurePosixPath(stripped)
    if result.is_absolute() or str(result) != stripped:
        raise ValueError("Noncanonical ZIP member path")
    return result


def verify_pe_header(data):
    if len(data) < 256 or data[:2] != b"MZ":
        raise ValueError("Bundled speech.exe has no Windows PE header")
    offset = int.from_bytes(data[60:64], "little")
    if not 64 <= offset <= 4096 or offset + 6 > len(data):
        raise ValueError("Bad Windows PE header offset")
    if data[offset:offset + 6] != b"PE\0\0\x64\x86":
        raise ValueError("Bundled speech.exe is not AMD64 PE")


def inspect_zip(zip_path):
    with zipfile.ZipFile(zip_path) as archive:
        infos = archive.infolist()
        if not 1 <= len(infos) <= MAX_FILES:
            raise ValueError("Unexpected Windows speech archive member count")
        seen = set()
        files = []
        total = 0
        for info in infos:
            rel = safe_member(info.filename)
            folded = str(rel).casefold()
            if folded in seen:
                raise ValueError("Case-colliding or duplicate archive paths")
            seen.add(folded)
            if info.flag_bits & 1:
                raise ValueError("Encrypted ZIP entries cannot be reviewed")
            unix_type = (info.external_attr >> 16) & 0o170000
            if unix_type == 0o120000:
                raise ValueError("Symlinked archive entry forbidden")
            if unix_type not in (0, 0o100000, 0o040000):
                raise ValueError("Unsupported ZIP entry type")
            if info.is_dir():
                if info.file_size != 0:
                    raise ValueError("Directory member contains unexpected bytes")
                continue
            if info.file_size < 0 or info.file_size > MAX_MEMBER:
                raise ValueError("Oversized native runtime archive member")
            total += info.file_size
            if total > MAX_TOTAL_UNPACKED:
                raise ValueError("Oversized native speech runtime ZIP expansion")
            files.append(str(rel))
        if "speech.exe" not in files:
            raise ValueError("Official Windows archive must contain root speech.exe")
        with archive.open("speech.exe") as exe:
            verify_pe_header(exe.read(4096))
    return files


def verify_official(archive_path):
    archive_path = Path(archive_path)
    if (archive_path.name != ARCHIVE_NAME or linked(archive_path) or
            not archive_path.is_file() or archive_path.stat().st_size != ARCHIVE_SIZE):
        raise ValueError("Not the pinned official speech.cpp Windows archive")
    checksum = hashlib.sha256()
    with archive_path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            checksum.update(block)
    if checksum.hexdigest() != ARCHIVE_SHA256:
        raise ValueError("Pinned GitHub release SHA-256 mismatch")
    return inspect_zip(archive_path)


def stage(archive_path, destination):
    files = verify_official(archive_path)
    destination = Path(destination).absolute()
    if destination.exists() or linked(destination):
        raise ValueError("Refusing to overwrite an existing runtime directory")
    # A caller must choose a real local destination, not a junction to an
    # unrelated tree. Parent creation is limited to an explicit existing dir.
    parent = destination.parent
    if not parent.is_dir():
        raise ValueError("Choose an existing local parent directory")
    ancestor = parent
    while True:
        if linked(ancestor):
            raise ValueError("Refusing a symlink/junction staging parent")
        if ancestor.parent == ancestor:
            break
        ancestor = ancestor.parent
    temporary = Path(tempfile.mkdtemp(prefix=".ember-speech-stage-", dir=parent))
    try:
        with zipfile.ZipFile(archive_path) as archive:
            for name in files:
                target = temporary.joinpath(*PurePosixPath(name).parts)
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(name) as src, target.open("xb") as dst:
                    shutil.copyfileobj(src, dst, length=1024 * 1024)
        if destination.exists():
            raise ValueError("Destination appeared during staging")
        temporary.rename(destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
    return len(files)


def fake_pe():
    data = bytearray(512)
    data[:2] = b"MZ"
    data[60:64] = (128).to_bytes(4, "little")
    data[128:134] = b"PE\0\0\x64\x86"
    return bytes(data)


def fixture_zip(entries):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w", zipfile.ZIP_DEFLATED) as out:
        for name, payload in entries:
            out.writestr(name, payload)
    stream.seek(0)
    return stream


def self_test():
    with tempfile.TemporaryDirectory() as work:
        local = Path(work) / ARCHIVE_NAME
        good = [("speech.exe", fake_pe()), ("README.txt", b"fixture-only")]
        local.write_bytes(fixture_zip(good).getvalue())
        assert inspect_zip(local) == ["speech.exe", "README.txt"]
        try:
            verify_official(local)
            raise AssertionError("Unpinned mock ZIP was accepted as official")
        except ValueError:
            pass
        for bad in (
            [("../escaped.txt", b"bad"), *good],
            [("speech.exe", fake_pe()), ("SPEECH.EXE", fake_pe())],
            [("speech.exe", b"MZ" + b"\0" * 254)],
            [("no-exe.txt", b"not Windows")],
            [("CON.txt", b"device"), *good],
            [("subdir\\\\wrong.txt", b"escape"), *good],
            [("folder/../escape.txt", b"bad"), *good],
        ):
            local.write_bytes(fixture_zip(bad).getvalue())
            try:
                inspect_zip(local)
                raise AssertionError("Unsafe or incomplete ZIP accepted")
            except ValueError:
                pass
        with zipfile.ZipFile(local, "w") as out:
            out.writestr("speech.exe", fake_pe())
            symlink = zipfile.ZipInfo("evil-link")
            symlink.create_system = 3
            symlink.external_attr = (0o120777 << 16)
            out.writestr(symlink, "../outside")
        try:
            inspect_zip(local)
            raise AssertionError("ZIP symlink was accepted")
        except ValueError:
            pass
    print("Pinned speech.cpp Windows archive and safe extraction self-test passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--output", type=Path,
                        help="Nonexisting engines/speech directory under existing parent")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.archive is None:
        parser.error("Specify an existing official Windows archive with --archive")
    try:
        if args.output:
            count = stage(args.archive, args.output)
            print(f"Verified pinned speech.cpp {VERSION}: staged {count} files without executing them")
        else:
            count = len(verify_official(args.archive))
            print(f"Verified pinned speech.cpp {VERSION} Windows ZIP: {count} safe files")
    except (OSError, ValueError, zipfile.BadZipFile, RuntimeError) as exc:
        parser.error(f"Offline speech runtime verification refused: {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
