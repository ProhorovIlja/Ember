#!/usr/bin/env python3
"""Bounded, read-only inspection of speech.cpp v0.11.2 GGUF model metadata.

No tensors are loaded, no model executes and no downloads occur. Validates the
actual Qwen3 model architecture, speech task, size, languages, and English
speaker metadata before a manually approved file is used in an offline build.
This is not a model-weight authenticity, inference or quality certification.

GGUF layout and keys: speech.cpp reference/qwen3-{tts,asr}/convert.py.
"""
import argparse
import json
import re
from pathlib import Path
import struct
import tempfile

MAX_METADATA = 256 * 1024 * 1024  # tokenizer arrays can be large
MAX_KEYS = 100_000
MAX_ARRAY_ITEMS = 3_000_000
MAX_KEY_BYTES = 256
MAX_VALUE_BYTES = 64 * 1024 * 1024
KEEP_KEYS = {
    "general.architecture", "general.size_label", "general.languages",
    "speech.task", "speech.layout", "speech.requires",
    "speech.sample_rate", "speech.voices",
    "speech.voice_languages", "speech.voice_genders",
    "speech.voice_descriptions",
}
# GGUFValueType from gguf-py: unsigned/signed scalar and UTF-8 string values,
# plus homogeneous arrays. The arrays contain metadata, not model tensors.
SIZES = {0: 1, 1: 1, 2: 2, 3: 2, 4: 4, 5: 4, 6: 4,
         7: 1, 10: 8, 11: 8, 12: 8}
TYPE_STRING = 8
TYPE_ARRAY = 9
UINT_LIMIT = 2**64 - 1


class InvalidSpeechGGUF(ValueError):
    pass


class Reader:
    def __init__(self, stream, file_size):
        self.stream = stream
        self.limit = min(file_size, MAX_METADATA)
        self.file_size = file_size

    def read(self, count):
        if count < 0 or self.stream.tell() + count > self.limit:
            raise InvalidSpeechGGUF("GGUF metadata exceeds strict read bounds")
        data = self.stream.read(count)
        if len(data) != count:
            raise InvalidSpeechGGUF("GGUF metadata is truncated")
        return data

    def skip(self, count):
        if count < 0 or self.stream.tell() + count > self.limit:
            raise InvalidSpeechGGUF("Invalid GGUF metadata element length")
        self.stream.seek(count, 1)

    def u32(self):
        return struct.unpack("<I", self.read(4))[0]

    def u64(self):
        return struct.unpack("<Q", self.read(8))[0]

    def string(self, *, keep=False, max_bytes=MAX_VALUE_BYTES):
        size = self.u64()
        if size > max_bytes:
            raise InvalidSpeechGGUF("Oversized GGUF metadata string")
        if not keep:
            self.skip(size)
            return None
        try:
            return self.read(size).decode("utf-8", "strict")
        except UnicodeError as exc:
            raise InvalidSpeechGGUF("Invalid UTF-8 model metadata") from exc

    def value(self, value_type, *, keep=False):
        if value_type == TYPE_STRING:
            return self.string(keep=keep)
        if value_type == TYPE_ARRAY:
            subtype, count = self.u32(), self.u64()
            if count > MAX_ARRAY_ITEMS:
                raise InvalidSpeechGGUF("GGUF metadata array is too long")
            if subtype in SIZES:
                width = SIZES[subtype]
                if keep and count <= 64 and subtype in (0, 1, 2, 3, 4, 5, 7, 10, 11):
                    return [self.value(subtype, keep=True) for _ in range(count)]
                self.skip(width * count)
                return None
            if subtype != TYPE_STRING:
                raise InvalidSpeechGGUF("Unsupported GGUF array element type")
            if keep and count > 128:
                raise InvalidSpeechGGUF("Speech model has oversized identity array")
            result = []
            for _ in range(count):
                item = self.string(keep=keep)
                if keep:
                    result.append(item)
            return result if keep else None
        width = SIZES.get(value_type)
        if width is None:
            raise InvalidSpeechGGUF("Unsupported GGUF metadata value type")
        if not keep:
            self.skip(width)
            return None
        raw = self.read(width)
        if value_type in (0, 1, 2, 3, 4, 5, 10, 11):
            fmt = {0:"B", 1:"b", 2:"H", 3:"h", 4:"I", 5:"i",
                   10:"Q", 11:"q"}[value_type]
            return struct.unpack("<" + fmt, raw)[0]
        if value_type == 7:
            if raw[0] not in (0, 1):
                raise InvalidSpeechGGUF("Malformed GGUF boolean")
            return bool(raw[0])
        return struct.unpack("<f" if value_type == 6 else "<d", raw)[0]


def metadata(path):
    file = Path(path)
    if not file.is_file() or file.is_symlink() or (
            hasattr(file, "is_junction") and file.is_junction()):
        raise InvalidSpeechGGUF("Model must be an existing ordinary local file")
    file_size = file.stat().st_size
    if file_size < 1024 * 1024:
        raise InvalidSpeechGGUF("Speech model is too small for a GGUF")
    with file.open("rb") as stream:
        reader = Reader(stream, file_size)
        if reader.read(4) != b"GGUF":
            raise InvalidSpeechGGUF("Missing GGUF magic")
        if reader.u32() not in (2, 3):
            raise InvalidSpeechGGUF("Unsupported GGUF version")
        tensors, count = reader.u64(), reader.u64()
        if not (1 <= tensors <= 200_000 and 1 <= count <= MAX_KEYS):
            raise InvalidSpeechGGUF("Invalid GGUF tensor or metadata count")
        output = {}
        for _ in range(count):
            key = reader.string(keep=True, max_bytes=MAX_KEY_BYTES)
            if not key or key in output:
                raise InvalidSpeechGGUF("Duplicate or empty GGUF metadata key")
            value_type = reader.u32()
            keep = key in KEEP_KEYS
            value = reader.value(value_type, keep=keep)
            if keep:
                output[key] = value
        return output


def model_identity(path, alias, *, selected_voice=None):
    if alias not in ("qwen3-tts-0.6b", "qwen3-tts-1.7b",
                     "qwen3-asr-0.6b", "qwen3-asr-1.7b"):
        raise InvalidSpeechGGUF("Unsupported Ember native speech alias")
    meta = metadata(path)
    family, size = alias.rsplit("-", 1)
    task = "synthesis" if family == "qwen3-tts" else "recognition"
    for key, expected in (
            ("general.architecture", family),
            ("general.size_label", size[:-1].upper() + "B"),
            ("speech.task", task),
            ("speech.layout", 1)):
        if meta.get(key) != expected:
            raise InvalidSpeechGGUF(
                f"Wrong GGUF {key}: expected {expected!r}; "
                f"found {str(meta.get(key))[:64]!r}")
    # speech.cpp converters set a minimum engine version in GGUF. A future
    # model layout must not silently pass validation against Ember's pinned
    # v0.11.2 runtime, even if someone relabels its GGUF file.
    required = meta.get("speech.requires")
    if not isinstance(required, str) or not re.fullmatch(
            r"[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}", required):
        raise InvalidSpeechGGUF("Missing or malformed speech.cpp minimum version")
    version = tuple(map(int, required.split(".")))
    if version > (0, 11, 2):
        raise InvalidSpeechGGUF(
            f"Native model requires speech.cpp {required}; Ember pins v0.11.2")
    languages = meta.get("general.languages")
    if not isinstance(languages, list) or "en" not in languages:
        raise InvalidSpeechGGUF("Speech model does not declare English support")
    english_voices = []
    english_female_voices = []
    if task == "recognition":
        if "ru" not in languages:
            raise InvalidSpeechGGUF("Ember RU/EN ASR needs both model languages")
    else:
        voices = meta.get("speech.voices")
        voice_langs = meta.get("speech.voice_languages")
        genders = meta.get("speech.voice_genders")
        descriptions = meta.get("speech.voice_descriptions")
        if (not isinstance(voices, list) or not isinstance(voice_langs, list) or
                not isinstance(genders, list) or
                not isinstance(descriptions, list) or
                not 1 <= len(voices) <= 128 or len(voices) != len(voice_langs) or
                len(voices) != len(genders) or len(voices) != len(descriptions)):
            raise InvalidSpeechGGUF(
                "TTS GGUF is missing paired voice/language/gender/description metadata")
        for voice, lang, gender, desc in zip(voices, voice_langs, genders, descriptions):
            if (not isinstance(voice, str) or
                    not 1 <= len(voice) <= 80 or
                    not all(ch.isascii() and (ch.isalnum() or ch in "_-")
                            for ch in voice) or
                    not isinstance(lang, str) or lang not in languages or
                    gender not in ("female", "male") or
                    not isinstance(desc, str) or not 1 <= len(desc) <= 1024):
                raise InvalidSpeechGGUF("Malformed native voice identity metadata")
            if lang == "en" and voice not in english_voices:
                english_voices.append(voice)
            if lang == "en" and gender == "female" and voice not in english_female_voices:
                english_female_voices.append(voice)
        if not english_female_voices:
            raise InvalidSpeechGGUF(
                "Stable Ember voice needs an English female speaker in the model")
        if selected_voice is not None and selected_voice not in english_female_voices:
            raise InvalidSpeechGGUF(
                "Selected voice is not an English female speaker in this GGUF")
    rate = meta.get("speech.sample_rate")
    if not isinstance(rate, int) or isinstance(rate, bool) or not 8000 <= rate <= 96000:
        raise InvalidSpeechGGUF("Invalid native speech audio sample rate")
    return {
        "architecture": family, "size_label": size[:-1].upper() + "B",
        "task": task, "languages": sorted(set(languages)),
        "english_speakers": english_voices,
        "english_female_speakers": english_female_voices,
        "sample_rate_hz": rate, "minimum_speechcpp": required,
        "verified_structure_only": True,
    }


def _fixture_gguf(family, size, task, *, langs=("en", "ru"),
                  voices=("vivian", "serena"), voice_langs=("en", "en"),
                  genders=("female", "female"),
                  duplicated_key=False, truncated=False):
    """Synthetic padded GGUF fixture: no real model weights or inference."""
    def string(s):
        x = s.encode("utf-8")
        return struct.pack("<Q", len(x)) + x
    fields = [
        ("general.architecture", 8, family),
        ("general.size_label", 8, size),
        ("speech.task", 8, task),
        ("speech.layout", 4, 1),
        ("speech.requires", 8, "0.7.0"),
        ("speech.sample_rate", 4, 24000 if task == "synthesis" else 16000),
        ("general.languages", 9, list(langs)),
        ("speech.voices", 9, list(voices)),
        ("speech.voice_languages", 9, list(voice_langs)),
        ("speech.voice_genders", 9, list(genders)),
        ("speech.voice_descriptions", 9, ["Synthetic speaker"] * len(voices)),
        ("ignored.large.tensor.metadata", 9, ["ignored"] * 3),
    ]
    if duplicated_key:
        fields.append(("speech.task", 8, task))
    raw = bytearray(b"GGUF" + struct.pack("<IQQ", 3, 1, len(fields)))
    for key, kind, value in fields:
        raw.extend(string(key) + struct.pack("<I", kind))
        if kind == 8:
            raw.extend(string(value))
        elif kind == 4:
            raw.extend(struct.pack("<I", value))
        else:
            raw.extend(struct.pack("<IQ", 8, len(value)))
            for element in value:
                raw.extend(string(element))
    if truncated:
        return bytes(raw[:68]).ljust(1024 * 1024 + 1, b"\0")
    return bytes(raw).ljust(1024 * 1024 + 1, b"\0")


def self_test():
    with tempfile.TemporaryDirectory(prefix="ember-gguf-meta-") as temp:
        file = Path(temp) / "model.gguf"
        for alias, family, size, task in (
            ("qwen3-tts-0.6b", "qwen3-tts", "0.6B", "synthesis"),
            ("qwen3-tts-1.7b", "qwen3-tts", "1.7B", "synthesis"),
            ("qwen3-asr-0.6b", "qwen3-asr", "0.6B", "recognition"),
            ("qwen3-asr-1.7b", "qwen3-asr", "1.7B", "recognition"),
        ):
            file.write_bytes(_fixture_gguf(family, size, task))
            detail = model_identity(file, alias,
                selected_voice="vivian" if task == "synthesis" else None)
            assert detail["architecture"] == family
            assert detail["minimum_speechcpp"] == "0.7.0"
            assert (detail["english_female_speakers"] == ["vivian", "serena"]
                    if task == "synthesis" else
                    detail["english_female_speakers"] == [])
            assert (detail["english_speakers"] == ["vivian", "serena"]
                    if task == "synthesis" else
                    detail["english_speakers"] == [])
            for wrong in (
                ("qwen3-asr-0.6b" if task == "synthesis" else "qwen3-tts-0.6b"),
                ("qwen3-tts-1.7b" if alias == "qwen3-tts-0.6b" else "qwen3-tts-0.6b"),
            ):
                if wrong == alias: continue
                try:
                    model_identity(file, wrong)
                    raise AssertionError("Wrong native speech family/size accepted")
                except InvalidSpeechGGUF:
                    pass
            if task == "synthesis":
                file.write_bytes(_fixture_gguf(
                    family, size, task, genders=("male", "female")))
                assert model_identity(file, alias, selected_voice="serena")[
                    "english_female_speakers"] == ["serena"]
                try:
                    model_identity(file, alias, selected_voice="vivian")
                    raise AssertionError("English male speaker accepted")
                except InvalidSpeechGGUF:
                    pass
                file.write_bytes(_fixture_gguf(family, size, task))
                for wrong_voice in ("sohee", "unavailable"):
                    try:
                        model_identity(file, alias, selected_voice=wrong_voice)
                        raise AssertionError("Missing English voice accepted")
                    except InvalidSpeechGGUF:
                        pass
        for bad in (
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis",
                          langs=("ru",)),
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis",
                          voices=("ono_anna",), voice_langs=("ja",)),
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis",
                          voices=("a", "b"), voice_langs=("en",)),
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis",
                          genders=("male", "male")),
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis",
                          genders=("female",)),
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis", duplicated_key=True),
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis", truncated=True),
            _fixture_gguf("qwen3-tts", "0.6B", "synthesis").replace(
                b"0.7.0", b"9.7.0", 1),
            _fixture_gguf("qwen3-asr", "1.7B", "recognition", langs=("en",)),
            b"GGUF" + struct.pack("<IQQ", 3, 0, 1) + bytes(1024 * 1024),
        ):
            file.write_bytes(bad)
            try:
                model_identity(file,
                    "qwen3-asr-1.7b" if b"qwen3-asr" in bad[:256] else "qwen3-tts-0.6b")
                raise AssertionError("Malformed or wrong GGUF accepted")
            except InvalidSpeechGGUF:
                pass
    print("speech.cpp GGUF metadata identity self-test passed (synthetic only)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--model", type=Path)
    parser.add_argument("--alias",
                        choices=("qwen3-tts-0.6b", "qwen3-tts-1.7b",
                                 "qwen3-asr-0.6b", "qwen3-asr-1.7b"))
    parser.add_argument("--voice", default=None)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.model or not args.alias:
        parser.error("--model and --alias required")
    try:
        print(json.dumps(model_identity(args.model, args.alias,
                                        selected_voice=args.voice),
                         indent=2))
    except (OSError, ValueError) as exc:
        parser.error(f"Native GGUF identity check failed: {exc}")


if __name__ == "__main__":
    main()
