#!/usr/bin/env python3
"""One-shot offline release-tool preflight, explicitly invoked by a build.

Only local synthetic fixtures; no network access, downloads, model inference,
scheduled tasks, background workers, GitHub Actions or release publication.
Run before copying gigabytes of approved weights or compiling Qt.
"""
import sys

from offline_bundle import self_test as offline_manifest_test
from portable_zip import self_test as portable_zip_test
from installer_bundle import self_test as installer_bundle_test
from windows_pe_audit import self_test as pe_imports_test


def run_preflight():
    # Legacy fixture tests intentionally use assert. -O would remove those
    # assertions and create a dangerous false-positive release preflight.
    if not __debug__ or sys.flags.optimize:
        raise RuntimeError("Packaging preflight cannot run with Python optimization (-O).")
    checks = (
        ("approved offline asset inventory and staging", offline_manifest_test),
        ("Windows import-table dependency closure", pe_imports_test),
        ("ZIP64 portable content, CRC/SHA and Windows path safety", portable_zip_test),
        ("complete Inno disk-spanned installer archive", installer_bundle_test),
    )
    for label, check in checks:
        print(f"Foreground packaging self-test: {label}", flush=True)
        check()
    print("All offline packaging tool self-tests passed. This is NOT real Windows/model acceptance.")
    return len(checks)


if __name__ == "__main__":
    try:
        run_preflight()
    except Exception as error:
        print(f"BLOCKED: packaging preflight failed: {type(error).__name__}: {error}",
              file=sys.stderr)
        sys.exit(2)
