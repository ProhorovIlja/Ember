# Ember

**Local-first voice and text AI for Windows.** Original native **C++20 + Qt 6 Quick** desktop UI. No WebView2, browser wrapper, Android emulator, cloud account, or compulsory subscription.

This repository currently contains a **development foundation**. It compiles an application and provides real integration points for local engines, but this is **not yet a fully tested release or a real-time, full-duplex conversation system**. No models or vendor executables are redistributed. Follow [the setup guide](docs/SETUP.md) and [release gates](docs/RELEASE-GATES.md).

## Design priorities

- Speech first: explicitly start the microphone → local `whisper.cpp` transcription → local `llama.cpp` response → `sherpa-onnx` Kokoro synthesis → Windows audio playback.
- Text chat: the same local LLM can be used without a microphone. Current session is held in memory, not silently uploaded.
- Efficient native UI: GPU-rendered Qt Quick, subtle animations, dark colors, original procedural voice stage, keyboard-controlled composer. No runtime web UI.
- Replaceable intelligence: separate speech recognition, reasoning, voice generation and desktop UI; measure each against alternatives instead of assuming that merging incompatible model weights will help.
- Portable storage under `D:\Games\Ember` by default, including `data/ember.ini`, per-session cache, engines and GGUF/ONNX weights. The actual install location is user-selected.
- Permission boundaries for future local files, programs, Internet and GitHub/Drive automation. No universal implicit access is granted by a text prompt.

## Current behavior

The backend launches a local `llama-server` bound to `127.0.0.1:43821` with one GGUF model and a fixed `ember` API alias. It posts current-session messages to the local `/v1/chat/completions` endpoint, never a remote URL. Audio recording runs only on explicit command and stops after 30 seconds. The first voice path is **turn-based**, not streaming. The caller must supply compatible engine binaries and licensed models; not every microphone can capture directly at 16 kHz, and unsupported devices are rejected rather than silently resampled. The built-in Kokoro path currently targets the **English v0.19 voice pack**. Russian speech output and multilingual voice quality are future acceptance items, not implied by the English model.

### Build

Requires MSVC x64, CMake 3.21+ and dynamically linked Qt 6.5+ with Quick, QuickControls2, Multimedia and Widgets.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
windeployqt --qmldir qml --release build\Release\Ember.exe
```

Copy the deployed executable and Qt DLLs into `D:\Games\Ember`; place external engine binaries under `engines` and separately downloaded models under `models` following [SETUP.md](docs/SETUP.md). Then open **Settings**, verify all paths, start the local brain, and test the selected voice.

## Development state and downloads

GitHub Actions builds an **unsigned, temporary development artifact** for testing source changes. It is **not** a complete offline model distribution and should not be mistaken for an approved stable release. [Releases](https://github.com/ProhorovIlja/Ember/releases) will publish an installer, versioned notes, checksums, license notices and source archives only after the release acceptance gates pass.

References and licensing: [docs/RESEARCH.md](docs/RESEARCH.md). Technical architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The Ember app code is MIT-licensed; Qt, models, GGUF converters and engine binaries retain their own licenses.
