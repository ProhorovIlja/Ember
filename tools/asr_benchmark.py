#!/usr/bin/env python3
"""Measure opt-in localhost speech.cpp ASR accuracy without storing output audio/transcripts."""
import argparse
import http.client
import io
import json
import re
import subprocess
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from localhost_owner import listening_owner_pids, require_owned_speech_listener

MODELS = ("qwen3-asr-0.6b", "qwen3-asr-1.7b")
LANGUAGES = ("auto", "en", "ru")
MAX_WAV = 25 * 1024 * 1024
MAX_TEXT = 65536
BOUNDARY = "----EmberASRBoundary562f"


def transcribe(wav_data, model, language, port=43822, omit_model_id=False,
               *, expected_pid=None, _fixture=False):
    if not _fixture:
        # Before constructing a multipart containing the user's private WAV,
        # require the *explicitly selected* speech.exe process to own the port.
        require_owned_speech_listener(expected_pid, port)
    elif port == 43822:
        raise ValueError("Fixture-only PID bypass is forbidden on the live port")
    if model not in MODELS or language not in LANGUAGES:
        raise ValueError("Unsupported model or language")
    if len(wav_data) > MAX_WAV or wav_data[:4] != b"RIFF" or wav_data[8:12] != b"WAVE":
        raise ValueError("Invalid or oversized local PCM WAV")
    fields = [
        ("language", language.encode("ascii")),
        ("response_format", b"text"),
    ]
    if not omit_model_id:
        fields.insert(0, ("model", model.encode("ascii")))
    pieces = []
    for name, value in fields:
        pieces.append((f"--{BOUNDARY}\r\nContent-Disposition: form-data; "
                       f"name=\"{name}\"\r\n\r\n").encode("ascii") +
                      value + b"\r\n")
    pieces.append((f"--{BOUNDARY}\r\nContent-Disposition: form-data; "
                   "name=\"file\"; filename=\"microphone.wav\"\r\n"
                   "Content-Type: audio/wav\r\n\r\n").encode("ascii") +
                  wav_data + b"\r\n")
    pieces.append(f"--{BOUNDARY}--\r\n".encode("ascii"))
    body = b"".join(pieces)
    connection = http.client.HTTPConnection("127.0.0.1", port=port, timeout=180)
    started = time.perf_counter()
    try:
        connection.request("POST", "/v1/audio/transcriptions", body=body, headers={
            "Content-Type": f"multipart/form-data; boundary={BOUNDARY}",
            "Content-Length": str(len(body)),
        })
        response = connection.getresponse()
        transcript = response.read(MAX_TEXT + 1)
        elapsed_ms = (time.perf_counter() - started) * 1000
        if (response.status != 200 or
                not response.getheader("Content-Type", "").lower().startswith("text/plain") or
                response.getheader("X-Speech-Stop", "").lower() == "model_limit" or
                len(transcript) > MAX_TEXT):
            raise ValueError("Local ASR returned invalid, truncated or oversized text")
        if not _fixture:
            # A service exiting/rebinding during ASR must not be reported as
            # a trustworthy model result. This cannot eliminate all TOCTOU
            # races, but rejects ordinary stale/hijacked localhost replies.
            require_owned_speech_listener(expected_pid, port)
        return transcript.decode("utf-8", errors="strict").strip(), round(elapsed_ms, 2)
    finally:
        connection.close()


def levenshtein(first, second):
    if len(first) > len(second):
        first, second = second, first
    previous = list(range(len(first) + 1))
    for index, item in enumerate(second, 1):
        row = [index]
        for i, other in enumerate(first, 1):
            row.append(min(row[-1] + 1, previous[i] + 1,
                           previous[i - 1] + (item != other)))
        previous = row
    return previous[-1]


def metrics(reference, prediction):
    if len(reference) > 4000 or len(prediction) > 4000:
        raise ValueError("Accuracy probe supports at most 4,000 text characters")
    words_a = re.findall(r"\w+", reference.casefold())
    words_b = re.findall(r"\w+", prediction.casefold())
    chars_a = list("".join(words_a))
    chars_b = list("".join(words_b))
    word_edits = levenshtein(words_a, words_b)
    character_edits = levenshtein(chars_a, chars_b)
    return {
        "wer": round(word_edits / max(1, len(words_a)), 4),
        "cer": round(character_edits / max(1, len(chars_a)), 4),
        "word_edit_distance": word_edits,
        "character_edit_distance": character_edits,
        "reference_character_count": len(chars_a),
        "reference_word_count": len(words_a),
        "recognized_word_count": len(words_b),
    }


def silent_wav():
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(16000)
        writer.writeframes(b"\x00\x00" * 1600)
    return buffer.getvalue()


def self_test():
    class Mock(BaseHTTPRequestHandler):
        omit_model_id = False
        def do_POST(self):
            assert self.path == "/v1/audio/transcriptions"
            request = self.rfile.read(int(self.headers["Content-Length"]))
            assert b'name="file"' in request and b'name="language"' in request
            if self.omit_model_id:
                assert b'name="model"' not in request
            else:
                assert b'name="model"' in request and b'qwen3-asr-0.6b' in request
            assert b'name="response_format"' in request and b"RIFF" in request
            data = "The bell tower.".encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("X-Speech-Stop", "complete")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *_args):
            pass

    fixture = """Active Connections
      TCP    127.0.0.1:43822       0.0.0.0:0             LISTENING       1234
      TCP    127.0.0.1:43822       127.0.0.1:6000        ESTABLISHED    1234
      TCP    0.0.0.0:43822         0.0.0.0:0             LISTENING       5555
      TCP    127.0.0.1:43821       0.0.0.0:0             LISTENING       8888
    """
    assert listening_owner_pids(fixture, 43822) == {1234}
    assert listening_owner_pids(fixture, 43821) == {8888}
    assert listening_owner_pids("", 43822) == set()
    server = ThreadingHTTPServer(("127.0.0.1", 0), Mock)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        try:
            transcribe(silent_wav(), MODELS[0], "en", server.server_port)
            raise AssertionError("Private audio probe accepted an unowned PID")
        except ValueError:
            pass
        result, elapsed = transcribe(silent_wav(), MODELS[0], "en",
                                     server.server_port, _fixture=True)
        assert result == "The bell tower." and elapsed >= 0
        Mock.omit_model_id = True
        omitted, latency = transcribe(silent_wav(), MODELS[0], "ru",
                                      server.server_port, omit_model_id=True,
                                      _fixture=True)
        assert omitted == result and latency >= 0
        Mock.omit_model_id = False
        assert metrics("The bell tower.", result)["wer"] == 0
        assert metrics("hello world", "hello")["wer"] == 0.5
        assert metrics("Привет мир", "Привет мир")["cer"] == 0
        for bad in (b"BAD" * 16, b""):
            try:
                transcribe(bad, MODELS[0], "en", server.server_port,
                           _fixture=True)
                raise AssertionError("Invalid WAV accepted")
            except ValueError:
                pass
    finally:
        server.shutdown()
        server.server_close()
        worker.join(timeout=3)
    print("native_asr_benchmark local-only protocol self-test passed")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--file", type=Path, help="Existing, user-selected WAV file")
    p.add_argument("--reference-file", type=Path, help="Existing local UTF-8 reference transcript")
    p.add_argument("--model", choices=MODELS, default=MODELS[0])
    p.add_argument("--speech-pid", type=int,
                   help="Required: actual Windows PID of the trusted speech.exe "
                        "listener on 127.0.0.1:43822")
    p.add_argument("--language", choices=LANGUAGES, default="auto")
    p.add_argument("--omit-model-id", action="store_true",
                   help="Use locally started speech.cpp GGUF without catalog name")
    p.add_argument("--output", type=Path,
                   help="Optional JSON with metrics only; no recognized words or audio")
    args = p.parse_args()
    if args.self_test:
        self_test()
        return
    if args.file is None or args.reference_file is None:
        p.error("Both --file and --reference-file are required")
    if not args.speech_pid or args.speech_pid <= 0:
        p.error("--speech-pid is mandatory for private live ASR")
    try:
        require_owned_speech_listener(args.speech_pid)
    except (ValueError, OSError, subprocess.TimeoutExpired) as exc:
        p.error("Trusted local ASR process check failed: " + str(exc))
    if args.file.stat().st_size > MAX_WAV:
        p.error("WAV is larger than the 25 MiB service limit")
    data = args.file.read_bytes()
    expected = args.reference_file.read_text(encoding="utf-8")
    recognized, elapsed = transcribe(data, args.model, args.language,
                                     omit_model_id=args.omit_model_id,
                                     expected_pid=args.speech_pid)
    result = metrics(expected, recognized)
    result.update({
        "model": args.model,
        "language_hint": args.language,
        "wav_bytes": len(data),
        "asr_wall_ms": elapsed,
        "note": "Local model benchmark; no output transcript stored. WER/CER are not confidence scores.",
    })
    output = json.dumps(result, ensure_ascii=False, indent=2)
    if args.output:
        args.output.write_text(output + "\n", encoding="utf-8")
    print(output)


if __name__ == "__main__":
    main()
