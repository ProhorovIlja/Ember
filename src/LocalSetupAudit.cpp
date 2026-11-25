#include "LocalSetupAudit.h"
#include <QFile>
#include <QFileInfo>
#include <QStringList>

namespace EmberLocalSetup {
namespace {
struct Check {
    bool ok = false;
    QString detail;
};
constexpr qint64 kMinimalModelBytes = 1024 * 1024;
constexpr quint32 kMaxPeOffset = 4096;
quint32 little32(const QByteArray &b, int offset) {
    if (b.size() < offset + 4) return 0;
    return quint32(quint8(b.at(offset))) |
           (quint32(quint8(b.at(offset + 1))) << 8) |
           (quint32(quint8(b.at(offset + 2))) << 16) |
           (quint32(quint8(b.at(offset + 3))) << 24);
}
bool safeReadable(const QString &path, QFileInfo &info, QString &error) {
    if (path.trimmed().isEmpty()) {
        error = "Not configured. Choose a local file in Settings.";
        return false;
    }
    info.setFile(path);
    if (!info.exists() || !info.isFile() || !info.isReadable() ||
        info.isSymLink() || info.isJunction()) {
        error = "Missing, unreadable or a symlink/junction. Choose a regular local file.";
        return false;
    }
    return true;
}
Check inspectWindowsExecutable(const QString &path) {
    QFileInfo info;
    QString error;
    if (!safeReadable(path, info, error)) return {false, error};
    if (info.suffix().compare("exe", Qt::CaseInsensitive) != 0 ||
        info.size() < 256) {
        return {false, "Expected a Windows x64 .exe, not an empty or truncated file."};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, "Cannot open Windows executable."};
    }
    const QByteArray start = file.read(64);
    if (start.size() != 64 || start.left(2) != QByteArray("MZ", 2)) {
        return {false, "Not a Windows PE executable (MZ signature missing)."};
    }
    const quint32 peOffset = little32(start, 0x3c);
    if (peOffset < 64 || peOffset > kMaxPeOffset ||
        qint64(peOffset) + 6 > file.size() || !file.seek(peOffset)) {
        return {false, "Invalid PE header offset."};
    }
    const QByteArray header = file.read(6);
    if (header.size() != 6 || header.left(4) != QByteArray("PE\0\0", 4)) {
        return {false, "Invalid PE signature."};
    }
    const quint16 machine = quint16(quint8(header.at(4))) |
                            (quint16(quint8(header.at(5))) << 8);
    if (machine != 0x8664) {
        return {false, "Executable is not x64/AMD64. Use a native Windows x64 build."};
    }
    return {true, "Windows x64 PE header present; dependencies and execution NOT tested."};
}
Check inspectGguf(const QString &path) {
    QFileInfo info;
    QString error;
    if (!safeReadable(path, info, error)) return {false, error};
    if (info.suffix().compare("gguf", Qt::CaseInsensitive) != 0 ||
        info.size() < kMinimalModelBytes) {
        return {false, "Expected a substantial GGUF weight file (at least 1 MiB)."};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {false, "Cannot open GGUF weights."};
    const QByteArray header = file.read(8);
    if (header.size() != 8 || header.left(4) != QByteArray("GGUF", 4)) {
        return {false, "Not a GGUF file (magic signature mismatch)."};
    }
    const quint32 version = little32(header, 4);
    if (version != 2 && version != 3) {
        return {false, "Unsupported GGUF header version " +
            QString::number(version) + "; verify compatibility with llama.cpp."};
    }
    return {true, "GGUF v" + QString::number(version) +
        " header present; weights, tokenizer and GPU fit NOT verified."};
}
Check inspectWhisperWeights(const QString &path) {
    QFileInfo info;
    QString error;
    if (!safeReadable(path, info, error)) return {false, error};
    if (info.suffix().compare("bin", Qt::CaseInsensitive) != 0 ||
        info.size() < kMinimalModelBytes) {
        return {false, "Choose a complete multilingual whisper.cpp GGML .bin model."};
    }
    const QString filename = info.fileName().toLower();
    if (filename.endsWith(".en.bin")) {
        return {false, "English-only .en.bin model selected. Use multilingual ggml-*.bin for RU/EN."};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, "Cannot open Whisper GGML weights."};
    }
    // whisper.cpp legacy GGML magic 0x67676d6c (little endian: lmgg).
    if (file.read(4) != QByteArray("lmgg", 4)) {
        return {false, "Whisper GGML model signature is invalid. Choose a multilingual ggml-*.bin."};
    }
    return {true, "Whisper GGML header present; complete weights and RU/EN accuracy NOT verified."};
}
} // namespace

Report inspect(const Inputs &input) {
    const Check brainExe = inspectWindowsExecutable(input.brainExe);
    const Check brainModel = inspectGguf(input.brainModel);
    const Check asrExe = input.whisperSelected
        ? inspectWindowsExecutable(input.whisperExe)
        : Check{false, "Native speech.cpp ASR selected; check its configured server separately."};
    const Check asrModel = input.whisperSelected
        ? inspectWhisperWeights(input.whisperModel)
        : Check{false, "Whisper model check skipped; no native ASR inference was performed."};
    Report report;
    report.textFilesReady = brainExe.ok && brainModel.ok;
    report.dictationFilesReady = input.whisperSelected &&
        asrExe.ok && asrModel.ok && input.microphoneDetected;
    const auto status = [](bool ok) { return ok ? "OK" : "CHECK"; };
    QStringList lines{
        "EMBER · OFFLINE TEXT-FIRST PREFLIGHT",
        "Read-only file-header check. No engine launched, audio recorded, "
            "network request made or model downloaded.",
        "",
        "TEXT CHAT · " + QString(status(report.textFilesReady)),
        "  llama-server: " + brainExe.detail,
        "  GGUF model: " + brainModel.detail,
        "",
        "RU/EN DICTATION · " + QString(status(report.dictationFilesReady)),
        "  ASR executable: " + asrExe.detail,
        "  ASR weights: " + asrModel.detail,
        "  Microphone: " + QString(input.microphoneDetected
                                   ? "Windows input device detected."
                                   : "No selected Windows microphone detected."),
        "",
        report.textFilesReady
            ? "Next: Start brain, wait for the owned loopback health check, then test a real prompt."
            : "Next: in Settings, Browse to a trusted Windows x64 llama-server and GGUF.",
        report.dictationFilesReady
            ? "Next: test real RU/EN long dictation; Cancel / To draft / Transcribe & send."
            : "Next: install/select offline multilingual ASR and microphone; if speech.cpp ASR is selected, manually verify its native engine.",
        "",
        "Important: This is NOT a passing inference, ASR accuracy, license, "
            "CUDA DLL, VRAM or clean-machine acceptance test. "
            "No complete model weight or license integrity check was performed."
    };
    report.text = lines.join("\n");
    return report;
}
} // namespace EmberLocalSetup
