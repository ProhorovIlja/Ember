#!/usr/bin/env python3
"""Offline static PE import audit for a complete native Windows x64 Ember folder.

No network, subprocess, registry edits, scheduled tasks or code execution.
Inspect x64 PE import and delay-import descriptors, then check whether a DLL
resolves next to the importing binary, in the payload root or via a known
Windows system library/API set. Fail closed for missing non-system runtimes.

Limitations: LoadLibrary/dlopen, GPU drivers, OS feature versions and native
binary trust cannot be established by inspecting import tables. The Windows
installer must still be smoke-tested on a clean disconnected target.
"""
import argparse
import json
import mmap
import re
import struct
import tempfile
from pathlib import Path


class InvalidPE(ValueError):
    pass


# Explicit Windows 10 platform DLLs only; compiler/Qt/CUDA/ggml runtimes
# must be shipped in the payload even if the CI runner happens to have them.
WINDOWS_DLLS = frozenset("""
advapi32.dll apphelp.dll avrt.dll bcrypt.dll bcryptprimitives.dll cabinet.dll
cfgmgr32.dll clbcatq.dll comctl32.dll comdlg32.dll combase.dll crypt32.dll
cryptbase.dll cryptnet.dll cryptsp.dll d2d1.dll d3d9.dll d3d11.dll d3d12.dll
d3dcompiler_47.dll dcomp.dll dbghelp.dll dnsapi.dll dsound.dll dwrite.dll
dwmapi.dll dxgi.dll dxva2.dll gdi32.dll gdi32full.dll gdiplus.dll hid.dll
imm32.dll iphlpapi.dll kernel32.dll kernelbase.dll ksuser.dll mf.dll
mfcore.dll mfplat.dll mfreadwrite.dll mmdevapi.dll mpr.dll msvcrt.dll
mswsock.dll ncrypt.dll netapi32.dll normaliz.dll ntdll.dll ole32.dll
oleaut32.dll opengl32.dll powrprof.dll propsys.dll psapi.dll
rasapi32.dll rpcrt4.dll secur32.dll setupapi.dll shcore.dll shell32.dll
shlwapi.dll tdh.dll ucrtbase.dll urlmon.dll user32.dll userenv.dll
usp10.dll uxtheme.dll version.dll winhttp.dll wininet.dll winmm.dll
windowscodecs.dll winspool.drv wlanapi.dll ws2_32.dll wsock32.dll
""".split())
DLL_RE = re.compile(r"^[a-z0-9_.+-]{2,128}\.(?:dll|drv|exe)$")
MAX_BINARY = 4 * 1024 * 1024 * 1024
MAX_NODES = 6000


def _u16(blob, offset):
    if offset < 0 or offset + 2 > len(blob):
        raise InvalidPE("Truncated PE header")
    return struct.unpack_from("<H", blob, offset)[0]


def _u32(blob, offset):
    if offset < 0 or offset + 4 > len(blob):
        raise InvalidPE("Truncated PE header")
    return struct.unpack_from("<I", blob, offset)[0]


def pe_imports(path):
    """Return (ordinary_imports, delay_imports) lowercased, or raise InvalidPE."""
    file = Path(path)
    if file.stat().st_size > MAX_BINARY:
        raise InvalidPE("PE executable or library exceeds the static scan limit")
    # mmap avoids copying multi-hundred-megabyte NVIDIA/CUDA DLLs into
    # process RAM. Only the PE headers and tiny import-name regions are read.
    with file.open("rb") as source:
        with mmap.mmap(source.fileno(), 0, access=mmap.ACCESS_READ) as blob:
            return _parse_mapped_pe(blob)


def _parse_mapped_pe(blob):
    if len(blob) < 0x200 or blob[:2] != b"MZ":
        raise InvalidPE("Not a complete Windows PE executable")
    pe = _u32(blob, 0x3C)
    if pe < 0x40 or pe > min(len(blob) - 24, 1024 * 1024):
        raise InvalidPE("Invalid PE header pointer")
    if blob[pe:pe + 4] != b"PE\0\0":
        raise InvalidPE("Missing PE signature")
    machine, sections, _, _, _, optional_size, _ = struct.unpack_from(
        "<HHIIIHH", blob, pe + 4)
    if machine != 0x8664:
        raise InvalidPE("Not AMD64 Windows native code")
    if not 1 <= sections <= 96 or optional_size < 0xF0:
        raise InvalidPE("Invalid x64 PE section or optional-header count")
    opt = pe + 24
    section_offset = opt + optional_size
    if section_offset + 40 * sections > len(blob):
        raise InvalidPE("Truncated PE section directory")
    if _u16(blob, opt) != 0x20B:
        raise InvalidPE("Invalid PE32+ optional-header signature")
    size_headers = _u32(blob, opt + 0x3C)
    num_directories = _u32(blob, opt + 0x6C)
    if size_headers > len(blob) or num_directories > 32:
        raise InvalidPE("Invalid PE data-directory bounds")
    sections_map = []
    for i in range(sections):
        off = section_offset + 40 * i
        virtual_size, virtual_addr, raw_size, raw_ptr = struct.unpack_from(
            "<IIII", blob, off + 8)
        if raw_size and (raw_ptr + raw_size > len(blob) or raw_ptr == 0):
            raise InvalidPE("Invalid section raw-data bounds")
        sections_map.append((virtual_addr, max(virtual_size, raw_size),
                             raw_ptr, raw_size))

    def rva_offset(rva, need=1):
        if rva < size_headers and rva + need <= size_headers:
            return rva
        for addr, span, raw, raw_size in sections_map:
            if addr <= rva < addr + span and rva - addr + need <= raw_size:
                return raw + rva - addr
        raise InvalidPE("Import RVA does not point to mapped raw PE data")

    def name_at(rva):
        start = rva_offset(rva)
        end = blob.find(b"\0", start, min(start + 129, len(blob)))
        if end < 0 or end == start:
            raise InvalidPE("Missing or oversized import library name")
        try:
            name = blob[start:end].decode("ascii").lower()
        except UnicodeDecodeError as exc:
            raise InvalidPE("Non-ASCII import library name") from exc
        if not DLL_RE.fullmatch(name):
            raise InvalidPE("Unsafe import library name")
        return name

    def directory(index):
        if num_directories <= index:
            return (0, 0)
        offset = opt + 0x70 + index * 8
        if offset + 8 > section_offset:
            raise InvalidPE("PE data directory exceeds optional header")
        return struct.unpack_from("<II", blob, offset)

    def collect(index, descriptor, delay=False):
        rva, size = directory(index)
        if not rva and not size:
            return set()
        if not rva or not size or size > 1024 * 1024:
            raise InvalidPE("Malformed PE import directory")
        imports = set()
        count = min((size // descriptor) + 1, 4096)
        if count < 1:
            raise InvalidPE("Empty import descriptor directory")
        complete = False
        for i in range(count):
            at = rva_offset(rva + i * descriptor, descriptor)
            fields = struct.unpack_from("<" + "I" * (descriptor // 4),
                                        blob, at)
            if all(v == 0 for v in fields):
                complete = True
                break
            if delay:
                if fields[0] != 1:
                    raise InvalidPE("Unsupported VA-based PE delay-import table")
                name_rva = fields[1]
            else:
                name_rva = fields[3]
            imports.add(name_at(name_rva))
        if not complete:
            raise InvalidPE("Unterminated PE import directory")
        return imports

    return collect(1, 20), collect(13, 32, delay=True)


def is_system(name):
    return (name in WINDOWS_DLLS or name.startswith("api-ms-win-") or
            name.startswith("ext-ms-win-"))


def audit_tree(root):
    """Inspect every PE in a portable folder; return metrics + missing imports."""
    supplied = Path(root)
    if supplied.is_symlink() or (
            hasattr(supplied, "is_junction") and supplied.is_junction()):
        raise ValueError("Portable installation root must not be a link")
    root = supplied.resolve(strict=True)
    if not root.is_dir() or root.is_symlink():
        raise ValueError("Portable installation root must be a real directory")
    children = list(root.rglob("*"))
    if len(children) > MAX_NODES:
        raise ValueError("Portable installation has too many entries")
    for path in children:
        if path.is_symlink() or (hasattr(path, "is_junction") and path.is_junction()):
            raise ValueError("Portable installation contains linked paths")
    binaries = [p for p in children if p.is_file() and
                p.suffix.lower() in (".exe", ".dll", ".drv", ".pyd")]
    if not (root / "Ember.exe").is_file():
        raise ValueError("Portable installation is missing Ember.exe")
    if not binaries:
        raise ValueError("No Windows executables found")
    imported = {}
    by_path = {p.relative_to(root).as_posix().casefold(): p for p in binaries}
    for exe in binaries:
        imported[exe] = pe_imports(exe)
    findings = []
    for exe, (ordinary, delayed) in imported.items():
        for kind, names in (("required", ordinary), ("delay", delayed)):
            for dep in sorted(names):
                if is_system(dep):
                    continue
                parent_path = (exe.parent / dep).relative_to(root).as_posix().casefold()
                root_path = dep.casefold()
                if parent_path not in by_path and root_path not in by_path:
                    findings.append({
                        "binary": exe.relative_to(root).as_posix(),
                        "import": dep, "kind": kind,
                    })
    return {
        "schema": "ember-native-pe-imports-v1",
        "binaries_scanned": len(binaries),
        "required_import_edges": sum(len(x[0]) for x in imported.values()),
        "delayed_import_edges": sum(len(x[1]) for x in imported.values()),
        "missing_imports": findings,
        "passed": not findings,
        "note": ("Static x64 PE imports only. No dynamic LoadLibrary, host "
                 "GPU-driver, OS-version or clean-Windows compatibility proof."),
    }


def _mock_pe(ordinary=(), delayed=(), machine=0x8664):
    """Synthetic executable for deterministic CI; never written as a shipped binary."""
    data = bytearray(0x1000)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", data, 0x84, machine, 1, 0, 0, 0, 0xF0, 0x2022)
    opt = 0x98
    struct.pack_into("<H", data, opt, 0x20B)
    struct.pack_into("<I", data, opt + 0x3C, 0x200)
    struct.pack_into("<I", data, opt + 0x6C, 16)
    section = opt + 0xF0
    struct.pack_into("<8sIIIIIIHHI", data, section,
                     b".rdata\0\0", 0x800, 0x1000, 0x800, 0x200,
                     0, 0, 0, 0, 0x40000040)
    ptr = 0x500
    for directory_index, names, desc_size in (
            (1, ordinary, 20), (13, delayed, 32)):
        if not names:
            continue
        table = 0x300 if directory_index == 1 else 0x400
        rva = 0x1000 + table - 0x200
        struct.pack_into("<II", data, opt + 0x70 + 8 * directory_index,
                         rva, (len(names) + 1) * desc_size)
        for i, name in enumerate(names):
            descriptor_at = table + i * desc_size
            name_at = ptr
            raw = name.encode("ascii") + b"\0"
            data[name_at:name_at + len(raw)] = raw
            ptr += len(raw)
            if directory_index == 1:
                struct.pack_into("<I", data, descriptor_at + 12,
                                 0x1000 + name_at - 0x200)
            else:
                struct.pack_into("<II", data, descriptor_at, 1,
                                 0x1000 + name_at - 0x200)
    return bytes(data)


def self_test():
    with tempfile.TemporaryDirectory(prefix="ember-pe-fixtures-") as d:
        root = Path(d)
        (root / "Ember.exe").write_bytes(_mock_pe(
            ("KERNEL32.dll", "Qt6Core.dll", "VCRUNTIME140.dll"),
            ("WINMM.dll",)))
        (root / "Qt6Core.dll").write_bytes(_mock_pe(("USER32.dll",)))
        result = audit_tree(root)
        assert not result["passed"] and len(result["missing_imports"]) == 1
        assert result["missing_imports"][0]["import"] == "vcruntime140.dll"
        (root / "VCRUNTIME140.dll").write_bytes(_mock_pe())
        result = audit_tree(root)
        assert result["passed"] and result["binaries_scanned"] == 3
        assert result["required_import_edges"] == 4
        assert result["delayed_import_edges"] == 1
        (root / "Ember.exe").write_bytes(_mock_pe(("KERNEL32.dll",), ("CUDA64_12.dll",)))
        assert not audit_tree(root)["passed"]
        (root / "CUDA64_12.dll").write_bytes(_mock_pe())
        assert audit_tree(root)["passed"]
        (root / "Ember.exe").write_bytes(_mock_pe(machine=0x14C))
        try:
            audit_tree(root)
            raise AssertionError("32-bit binary accepted for native x64 payload")
        except InvalidPE:
            pass
        (root / "Ember.exe").write_bytes(b"MZ" + bytes(400))
        try:
            audit_tree(root)
            raise AssertionError("Corrupt PE accepted")
        except InvalidPE:
            pass
    print("Windows AMD64 PE import closure self-test passed (no executable run)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--root", type=Path, help="Existing native Windows app folder")
    parser.add_argument("--output", type=Path,
                        help="Optional new JSON metadata report only")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.root:
        parser.error("--root required except for --self-test")
    try:
        result = audit_tree(args.root)
        document = json.dumps(result, indent=2) + "\n"
        if args.output:
            if args.output.exists():
                raise ValueError("Refusing to replace existing PE audit report")
            args.output.write_text(document, encoding="utf-8")
        print(document, end="")
        if not result["passed"]:
            parser.exit(2, "Missing non-system Windows PE imports\n")
    except (OSError, InvalidPE, ValueError) as exc:
        parser.error(f"PE closure audit failed: {type(exc).__name__}: {exc}")


if __name__ == "__main__":
    main()
