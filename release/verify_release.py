#!/usr/bin/env python3
"""Fail-closed gate for future stable Ember GitHub Releases.

No acceptance file ships on the development branch. A release is deliberately
blocked until evidence from real Windows/NVIDIA/text/ASR tests is reviewed
and committed. Never claim that a passing native CI compile proves these gates.
"""
from __future__ import annotations

import argparse
import datetime as dt
import json
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parent.parent
REQUIRED = (
    "clean_windows_10_install",
    "nvidia_rtx_4070_text_model_test",
    "english_russian_long_asr",
    "dictation_cancel_draft_send",
    "english_voice_quality_real_listening",
    "native_tts_real_windows_playback",
    "spoken_reply_interruption_and_cleanup",
    "offline_voice_model_weights_and_dependencies",
    "offline_no_vpn_clean_install",
    "local_code_read_edit_test",
    "github_agent_real_tool_calls",
    "google_drive_consent_read",
    "long_multiturn_and_cancellation",
    "privacy_and_localhost_audit",
    "ui_accessibility_and_dpi",
    "model_and_qt_license_review",
    "installer_install_uninstall",
)
TAG = re.compile(r"^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$")
HASH = re.compile(r"^[0-9a-f]{40}$")


def valid_evidence_url(value: object) -> bool:
    if not isinstance(value, str) or len(value) > 2048:
        return False
    parsed = urlparse(value)
    return parsed.scheme == "https" and bool(parsed.netloc) and bool(parsed.path)


def validate_manifest(tag: str, doc: object, notes: str, cmake: str) -> list[str]:
    errors: list[str] = []
    match = TAG.fullmatch(tag)
    if not match:
        errors.append("Release tag must be stable SemVer vMAJOR.MINOR.PATCH.")
    if not isinstance(doc, dict):
        return errors + ["Missing structured acceptance evidence."]
    if doc.get("tag") != tag:
        errors.append("Acceptance tag does not match Git tag.")
    if doc.get("state") != "accepted":
        errors.append("Acceptance state must be exactly 'accepted'.")
    source_sha = doc.get("tested_source_commit")
    if not isinstance(source_sha, str) or not HASH.fullmatch(source_sha):
        errors.append("Missing 40-character tested_source_commit.")
    signed = doc.get("reviewed_by")
    if not isinstance(signed, str) or not signed.strip():
        errors.append("A human reviewer must approve the acceptance record.")
    timestamp = doc.get("reviewed_at")
    try:
        parsed = dt.datetime.fromisoformat(str(timestamp).replace("Z", "+00:00"))
        if parsed.tzinfo is None:
            raise ValueError("missing timezone")
    except ValueError:
        errors.append("reviewed_at must be a timezone-aware ISO-8601 date.")
    checks = doc.get("checks", {})
    if not isinstance(checks, dict):
        checks = {}
        errors.append("checks must be an object.")
    for name in REQUIRED:
        item = checks.get(name, {})
        if not isinstance(item, dict) or item.get("passed") is not True:
            errors.append(f"Unapproved gate: {name}.")
            continue
        if not valid_evidence_url(item.get("evidence_url")):
            errors.append(f"Missing HTTPS evidence URL for {name}.")
    if not match:
        return errors
    expected_note = f"release/notes/{tag}.md"
    if doc.get("release_notes") != expected_note:
        errors.append(f"Release notes must be {expected_note}.")
    if len(notes) < 450 or not all(
        section in notes.lower() for section in ("changes", "limitations", "models")
    ):
        errors.append("Release notes need detailed changes, models and limitations.")
    version = ".".join(match.groups())
    if not re.search(
        r"project\s*\(\s*Ember\s+VERSION\s+" + re.escape(version) + r"\b",
        cmake,
    ):
        errors.append(f"CMake project version does not match {version}.")
    return errors


def repository_changed_only_release_records(source_sha: str, tag: str) -> list[str]:
    commands = (
        ["git", "merge-base", "--is-ancestor", source_sha, "HEAD"],
        ["git", "diff", "--name-only", source_sha, "HEAD"],
    )
    for cmd in commands[:1]:
        try:
            subprocess.run(cmd, cwd=ROOT, check=True, capture_output=True, text=True)
        except (OSError, subprocess.CalledProcessError):
            return ["Accepted source revision is not an ancestor of the release tag."]
    changed = subprocess.run(
        commands[1], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout.splitlines()
    allowed = {"release/acceptance.json", f"release/notes/{tag}.md"}
    unexpected = sorted(set(changed) - allowed)
    return [f"Files changed since tested source commit: {', '.join(unexpected)}"] if unexpected else []


def self_test() -> None:
    tag = "v1.2.3"
    manifest = {
        "tag": tag,
        "state": "accepted",
        "tested_source_commit": "a" * 40,
        "reviewed_by": "acceptance reviewer",
        "reviewed_at": "2026-11-21T16:00:00+00:00",
        "release_notes": "release/notes/v1.2.3.md",
        "checks": {name: {"passed": True, "evidence_url": "https://example.org/report/1"}
                   for name in REQUIRED},
    }
    notes = ("# Ember release\n## Changes\nOffline text and dictation with verified "
             "quality and hardware testing.\n## Models\nTested weights and license inventory.\n"
             "## Limitations\nPublished transparent residual limitations.\n") * 5
    cmake = "project(Ember VERSION 1.2.3 LANGUAGES CXX)"
    assert not validate_manifest(tag, manifest, notes, cmake)
    manifest["checks"]["dictation_cancel_draft_send"]["passed"] = False
    assert any("dictation_cancel_draft_send" in error
               for error in validate_manifest(tag, manifest, notes, cmake))
    manifest["checks"]["dictation_cancel_draft_send"]["passed"] = True
    manifest["checks"]["local_code_read_edit_test"]["passed"] = False
    assert any("local_code_read_edit_test" in error
               for error in validate_manifest(tag, manifest, notes, cmake))
    manifest["checks"]["local_code_read_edit_test"]["passed"] = True
    for required_voice_gate in (
        "english_voice_quality_real_listening",
        "native_tts_real_windows_playback",
        "spoken_reply_interruption_and_cleanup",
        "offline_voice_model_weights_and_dependencies",
        "offline_no_vpn_clean_install",
    ):
        manifest["checks"][required_voice_gate]["passed"] = False
        assert any(required_voice_gate in error
                   for error in validate_manifest(tag, manifest, notes, cmake))
        manifest["checks"][required_voice_gate]["passed"] = True
    assert validate_manifest("v1.2.3-rc1", manifest, notes, cmake)
    assert validate_manifest(tag, manifest, "# only a placeholder", cmake)
    assert not valid_evidence_url("http://127.0.0.1:43822/private")
    print("Stable release gate self-tests passed.")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tag", help="Git tag to validate, e.g. v1.0.0")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.tag:
        parser.error("--tag is required except for --self-test")
    source = ROOT / "release" / "acceptance.json"
    if not source.is_file():
        print("BLOCKED: release/acceptance.json has not been approved or committed.")
        return 2
    try:
        doc = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        print(f"BLOCKED: invalid acceptance record: {exc}")
        return 2
    note_path = ROOT / "release" / "notes" / f"{args.tag}.md"
    notes = note_path.read_text(encoding="utf-8") if note_path.is_file() else ""
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    errors = validate_manifest(args.tag, doc, notes, cmake)
    if isinstance(doc, dict) and isinstance(doc.get("tested_source_commit"), str):
        if HASH.fullmatch(doc["tested_source_commit"]):
            errors.extend(repository_changed_only_release_records(
                doc["tested_source_commit"], args.tag
            ))
    if errors:
        print("BLOCKED: stable GitHub release gates are not met.")
        for error in errors:
            print(" - " + error)
        return 2
    print("ACCEPTED: stable release gates and source revision verified.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
