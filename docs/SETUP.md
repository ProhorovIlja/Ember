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
            lexicon-en-us.txt
```

1. Get **Windows x64/CUDA** `llama-server` from the upstream [llama.cpp](https://github.com/ggml-org/llama.cpp) releases or compile its CMake project with CUDA support. Preserve its accompanying DLL files next to the executable. Select a *verified GGUF conversion* of an open-license chat model; for a 12 GB RTX 4070, start benchmarking an approximately 8–9B-class model at Q4 rather than assuming all 16-bit weights will fit. There is no bundled or automatically downloaded model.
2. Get `whisper-cli` from [whisper.cpp](https://github.com/ggml-org/whisper.cpp) (build with optional CUDA) and a multilingual GGML model (e.g. `ggml-base.bin` from upstream). The microphone first requests 16 kHz mono Int16. If that is unavailable, the app accepts the device's preferred UInt8/Int16/Int32/Float PCM format and uses a bounded windowed-sinc filter to resample it into 16 kHz mono PCM for Whisper.
3. Get the offline TTS executable and an **English Kokoro** model pack from [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx). The app expects `model.onnx`, `voices.bin`, `tokens.txt`, and `lexicon-en-us.txt` **in the selected Kokoro folder**. The lexicon may be provided as a separate upstream file; place it there. Retain every model/engine license. Voice selection is by the **installed pack's real numerical speaker ID**; the app does not invent commercial character names. For multilingual output, a separate Kokoro pack and extra phoneme resources will require a reviewed adapter.
4. Build the Qt app with MSVC / Qt 6 or use a successful unsigned CI **development artifact**. Run `windeployqt --qmldir qml --release build\Release\Ember.exe`. Copy its deployed output to your preferred D-drive folder.
5. Open Settings and use native **Browse** buttons for all six model/engine paths. Start brain; wait for the local `/health` check to pass. Try text chat, then **Test voice**, then **Record voice** and **Stop recording**.

Engine paths are explicit, not dynamically downloaded. Never copy random executable builds from unknown repositories. All model inference and voice synthesis occur locally if you use the upstream binaries and configuration described.

### Known limitations

- Not live full-duplex or streaming audio; latency includes one process startup per ASR/TTS request.
- Common 44.1/48 kHz mono/stereo microphones are resampled to 16 kHz with anti-alias filtering; explicit device selection, VAD, echo cancellation, barge-in and USB/Bluetooth hotplug recovery remain pending.
- The first Kokoro model is English. Russian synthesis and multilingual quality require a verified compatible engine/model path.
- No model weight, audio or Windows packaging QA has been run on the user's own RTX 4070. A successful CI compile is not that test.
- No file/tool control, autonomous Internet/GitHub/Drive, multimodal vision or content generation is included in the first milestone.
