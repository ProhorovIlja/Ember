# Windows portable setup — first voice milestone

**Target computer:** Ryzen 5 5600 / NVIDIA RTX 4070 (12 GB VRAM) / 32 GB RAM / two SSDs. Preferred installation `D:\Games\Ember`. This path is *recommended*, not hardcoded: the application uses its executable folder for portable settings and private audio scratch data.

## Portable folder layout

```text
D:\Games\Ember\
    Ember.exe                 (Qt deployment)
    Qt6Core.dll, ...          (windeployqt result)
    data\ember.ini           (created on first config save)
    cache\                   (temporary session WAV files; deleted on clean exit)
    engines\
        llama\llama-server.exe
        whisper\whisper-cli.exe
        sherpa\sherpa-onnx-offline-tts.exe
    models\
        llm\model.gguf
        stt\ggml-base.bin
        tts\kokoro-en-v0_19\
            model.onnx
            voices.bin
            tokens.txt
            espeak-ng-data\       (phonemization resources)
```

1. Get **Windows x64/CUDA** `llama-server` from the upstream [llama.cpp](https://github.com/ggml-org/llama.cpp) releases or compile its CMake project with CUDA support. Preserve its accompanying DLL files next to the executable. Select a *verified GGUF conversion* of an open-license chat model; for a 12 GB RTX 4070, start benchmarking an approximately 8–9B-class model at Q4 rather than assuming all 16-bit weights will fit. There is no bundled or automatically downloaded model.
2. Get `whisper-cli` from [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (build with optional CUDA) and a multilingual GGML model (e.g. `ggml-base.bin` from upstream). The microphone first requests 16 kHz mono Int16. If that is unavailable, the app accepts the device's preferred UInt8/Int16/Int32/Float PCM format and uses a bounded windowed-sinc filter to resample it into 16 kHz mono PCM for Whisper.
3. Get the offline TTS executable and an **English Kokoro** model pack from [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx). The app expects `model.onnx`, `voices.bin`, `tokens.txt`, and the **`espeak-ng-data` subfolder** from the original English **v0.19** package. The official v0.19 command uses `--kokoro-data-dir` and **does not require a lexicon file**. Retain the upstream licenses. The native selector names all eleven verified IDs (0–10): af, Bella, Nicole, Sarah, Sky, Adam, Michael, Emma, Isabella, George and Lewis. Settings include an **editable voice comparison script** (up to 1200 characters); save it and choose **Test voice** to hear identical content on each candidate. For multilingual output, a separate Kokoro pack and extra phoneme resources will require a reviewed adapter.
4. Build the Qt app with MSVC / Qt 6 or use a successful unsigned CI **development artifact**. Run `windeployqt --qmldir qml --release build\Release\Ember.exe`. Copy its deployed output to your preferred D-drive folder.
5. Open **Settings**, choose your **Microphone** and **Speakers** by name, and use the native **Browse** buttons for each selected model/engine. Device preferences persist by opaque system ID rather than list position; if hardware disappears, Ember stops the affected capture or playback and falls back to Windows defaults.
6. For a more expressive/multilingual **alternative to Kokoro**, install a separate licensed self-hosted speech server exposing `POST /v1/audio/speech` and configure it to listen on `127.0.0.1:43822`. Supported server families include [second-state/qwen3_audio_api](https://github.com/second-state/qwen3_audio_api), [Qwen3-TTS native server](https://github.com/gabriele-mastrapasqua/qwen3-tts), and [devnen/Chatterbox-TTS-Server](https://github.com/devnen/Chatterbox-TTS-Server). The protocol is optional: **Ember does not install, download or start these servers**. Select **Qwen3 / Chatterbox · local API**, set the server's actual model alias and speaker name (e.g. `qwen3-tts` + `Vivian` on a compatible Qwen server), then use **Test voice**. The endpoint is fixed to localhost port 43822. Ember expects a complete uncompressed 16-bit PCM WAV response (8 MiB maximum); services returning MP3/Opus/JSON or chunked streaming must be configured to return WAV. A failed/offline server shows an error rather than silently falling back to remote inference.
7. Start the brain and wait for local `/health`. Test typed chat, then speech synthesis, then **Record voice** → **Stop recording**. An RTX 4070 12 GB cannot be assumed to run a full-size LLM and multiple large GPU speech models simultaneously; first benchmark Kokoro on CPU with the GPU devoted to the LLM, then test Qwen 0.6B separately before enabling both.

Engine paths are explicit, not dynamically downloaded. Never copy random executable builds from unknown repositories. All model inference and voice synthesis occur locally if you use the upstream binaries and configuration described.

### Development installer

GitHub Actions builds `Ember-0.1.0-dev-Windows-x64.exe` plus the portable `Ember-dev-win64.zip` and a SHA-256 file. The per-user installer suggests `D:\Games\Ember` when a D drive exists and falls back to the user's local application directory. It has passed automatic install, native QML startup and uninstall on a Windows CI runner. The ZIP and installer **do not include model weights or third-party engine executables** and are only short-lived development artifacts, not stable GitHub Releases.

A later stable version requires human-reviewed hardware and voice benchmarks. The acceptance-gated tag workflow is in `.github/workflows/stable-release.yml` and only a corresponding version tag with `release/acceptance.json` and version-specific release notes can publish a stable release.

### Known limitations

- Not live full-duplex or streaming audio; latency includes one process startup per ASR/TTS request.
- Common 44.1/48 kHz mono/stereo microphones are resampled to 16 kHz with anti-alias filtering. Native input/output selection, persisted device IDs, capture memory watchdog and basic device-disconnection handling are implemented, but actual USB/Bluetooth fault testing, VAD, echo cancellation, barge-in and full-duplex remain pending.
- The first Kokoro model is English. Russian synthesis and multilingual quality require a verified compatible engine/model path.
- No model weight, audio or Windows packaging QA has been run on the user's own RTX 4070. A successful CI compile is not that test.
- No file/tool control, autonomous Internet/GitHub/Drive, multimodal vision or content generation is included in the first milestone.
