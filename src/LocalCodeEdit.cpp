#include "LocalCodeEdit.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace EmberLocalEdit {
namespace {
constexpr qint64 kMaximumFileBytes = 512 * 1024;
constexpr int kMaximumFindChars = 32768;
constexpr int kMaximumReplaceChars = 65536;
const QSet<QString> kAllowedExtensions{
    "txt", "md", "json", "yaml", "yml", "xml", "cpp", "cc",
    "c", "h", "hpp", "rs", "py", "js", "ts", "qml", "ini",
    "toml", "cmake", "diff", "patch", "cs", "go", "java",
    "kt", "swift", "sh", "ps1", "bat", "sql", "css", "html"
};
bool safeFile(const QFileInfo &info) {
    if (!info.exists() || !info.isFile() || !info.isReadable() ||
        !info.isWritable() || info.isSymLink() || info.isJunction())
        return false;
    const QString name = info.fileName().toLower();
    if (name.startsWith('.') || name.startsWith("id_rsa") ||
        name.startsWith("id_ed25519") || name.contains("credential") ||
        name.contains("private-key") || name.contains("service-account") ||
        name.endsWith(".pem") || name.endsWith(".key") ||
        name.endsWith(".pfx") || name.endsWith(".p12") ||
        name.endsWith(".kdbx")) return false;
    return kAllowedExtensions.contains(info.suffix().toLower()) ||
           name == "makefile" || name == "dockerfile" ||
           name == "cmakelists.txt";
}
bool validUtf8(const QByteArray &bytes) {
    if (bytes.contains('\0')) return false;
    const QString text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes &&
        !(bytes.startsWith("\xEF\xBB\xBF") &&
          text.toUtf8() == bytes.mid(3))) return false;
    for (char c : bytes) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (b < 0x20 && b != '\r' && b != '\n' && b != '\t') return false;
        if (b == 0x7f) return false;
    }
    return true;
}
QByteArray sha(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
bool exactSnapshot(const EditPlan &plan, QByteArray &current) {
    const QFileInfo info(plan.path);
    if (!safeFile(info) || info.canonicalFilePath() != plan.path ||
        info.size() > kMaximumFileBytes) return false;
    QFile file(plan.path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    current = file.read(kMaximumFileBytes + 1);
    return file.error() == QFileDevice::NoError &&
           current.size() <= kMaximumFileBytes &&
           sha(current) == plan.originalSha256 &&
           current == plan.originalBytes;
}
}
SearchReplaceBlock parseSingleSearchReplaceBlock(const QString &modelReply) {
    SearchReplaceBlock result;
    if (modelReply.isEmpty() || modelReply.size() > 200000) {
        result.error = "Model answer is empty or too large for edit parsing.";
        return result;
    }
    QString text = modelReply;
    text.replace("\r\n", "\n");
    const QString begin = QStringLiteral("<<<<<<< SEARCH\n");
    const QString divider = QStringLiteral("\n=======\n");
    const QString end = QStringLiteral("\n>>>>>>> REPLACE");
    const qsizetype start = text.indexOf(begin);
    const qsizetype middle = start < 0 ? -1 :
        text.indexOf(divider, start + begin.size());
    const qsizetype finish = middle < 0 ? -1 :
        text.indexOf(end, middle + divider.size());
    if (start < 0 || middle < 0 || finish < 0 ||
        text.indexOf(begin, start + begin.size()) >= 0 ||
        text.indexOf(divider, middle + divider.size()) >= 0 ||
        text.indexOf(end, finish + end.size()) >= 0) {
        result.error = "Expected exactly one SEARCH / REPLACE block. "
                       "Ask Ember for a single edit, or use manual mode.";
        return result;
    }
    result.oldText = text.mid(start + begin.size(),
                              middle - (start + begin.size()));
    result.newText = text.mid(middle + divider.size(),
                              finish - (middle + divider.size()));
    if (result.oldText.isEmpty() || result.oldText.size() > kMaximumFindChars ||
        result.newText.size() > kMaximumReplaceChars ||
        result.oldText == result.newText ||
        result.oldText.contains(QChar::Null) ||
        result.newText.contains(QChar::Null)) {
        result.error = "Model edit has empty, identical, malformed, or oversized text.";
        return result;
    }
    return result;
}
EditPlan planExactReplace(const QString &selectedPath,
                          const QString &exactOldText,
                          const QString &newText) {
    EditPlan result;
    if (exactOldText.isEmpty() ||
        exactOldText.size() > kMaximumFindChars ||
        newText.size() > kMaximumReplaceChars ||
        exactOldText == newText ||
        exactOldText.contains(QChar::Null) ||
        newText.contains(QChar::Null)) {
        result.error = "Supply different, nonempty old and new text "
                       "(maximum 32K/64K characters).";
        return result;
    }
    const QFileInfo info(selectedPath);
    if (!safeFile(info) || info.size() > kMaximumFileBytes) {
        result.error = "Choose a writable UTF-8 source file (up to 512 KiB); "
                       "private keys, symlinks, junctions and binaries are blocked.";
        return result;
    }
    result.path = info.canonicalFilePath();
    if (result.path.isEmpty()) {
        result.error = "Could not resolve selected local file.";
        return result;
    }
    QFile file(result.path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = "Could not open selected file.";
        return result;
    }
    result.originalBytes = file.read(kMaximumFileBytes + 1);
    if (file.error() != QFileDevice::NoError ||
        result.originalBytes.size() > kMaximumFileBytes ||
        !validUtf8(result.originalBytes)) {
        result.error = "Source must be complete valid UTF-8 text within 512 KiB.";
        return result;
    }
    const QByteArray before = exactOldText.toUtf8();
    const QByteArray after = newText.toUtf8();
    if (!validUtf8(before) || !validUtf8(after)) {
        result.error = "Replacement contains invalid text/control characters.";
        return result;
    }
    const qsizetype first = result.originalBytes.indexOf(before);
    if (first < 0 || result.originalBytes.indexOf(before, first + 1) >= 0) {
        result.error = first < 0
            ? "Exact old text not found. Nothing changed."
            : "Old text occurs more than once. Narrow the selection.";
        return result;
    }
    result.proposedBytes = result.originalBytes;
    result.proposedBytes.replace(first, before.size(), after);
    if (result.proposedBytes.size() > kMaximumFileBytes ||
        !validUtf8(result.proposedBytes)) {
        result.error = "Result would exceed 512 KiB or produce invalid text.";
        return result;
    }
    result.originalSha256 = sha(result.originalBytes);
    const QByteArray contextBefore =
        result.originalBytes.mid(qMax(qsizetype(0), first - 100), qMin(qsizetype(100), first));
    const QByteArray contextAfter =
        result.originalBytes.mid(first + before.size(), 100);
    result.displayPreview =
        QStringLiteral("FILE: %1\nOriginal SHA-256: %2\n"
                       "Original bytes: %3 | Proposed bytes: %4\n"
                       "One exact match; no other source files will change.\n\n"
                       "CONTEXT BEFORE:\n%5\n\nREMOVE:\n%6\n\nADD:\n%7\n\n"
                       "CONTEXT AFTER:\n%8\n\n"
                       "Apply only after reviewing this edit. A private local "
                       "backup is written before replacing the file.")
        .arg(result.path,
             QString::fromLatin1(result.originalSha256.toHex()),
             QString::number(result.originalBytes.size()),
             QString::number(result.proposedBytes.size()),
             QString::fromUtf8(contextBefore),
             exactOldText,
             newText,
             QString::fromUtf8(contextAfter));
    return result;
}
EditPlan planWithinProject(const QString &selectedRoot,
                           const QString &relativePath,
                           const QString &exactOldText,
                           const QString &newText) {
    const QFileInfo rootInfo(selectedRoot);
    if (!rootInfo.exists() || !rootInfo.isDir() ||
        rootInfo.isSymLink() || rootInfo.isJunction())
        return {{}, {}, "Project root must be an actual directory."};
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty() || relativePath.isEmpty() ||
        relativePath.size() > 240 || relativePath.startsWith('/') ||
        relativePath.endsWith('/') || relativePath.contains('\\') ||
        relativePath.contains(':') || relativePath.contains("//") ||
        relativePath.contains('\n') || relativePath.contains('\r') ||
        relativePath.contains(QChar::Null))
        return {{}, {}, "Invalid repository-relative source path."};
    static const QSet<QString> skipDirs{
        ".git", ".hg", ".svn", ".idea", ".vs", ".vscode",
        "venv", ".venv", "node_modules", "__pycache__", "cache",
        "build", "dist", "bin", "obj", "target", "vendor"
    };
    const QStringList components = relativePath.split('/');
    QDir parent(canonicalRoot);
    for (int index = 0; index < components.size(); ++index) {
        const QString component = components.at(index);
        if (component.isEmpty() || component == "." || component == ".." ||
            component.startsWith('.') ||
            (index + 1 < components.size() &&
             skipDirs.contains(component.toLower())))
            return {{}, {}, "Hidden, dependency or traversal source path denied."};
        const QFileInfo segment(parent.filePath(component));
        if (!segment.exists() || segment.isSymLink() || segment.isJunction() ||
            (index + 1 < components.size() && !segment.isDir()))
            return {{}, {}, "Missing or linked project file component."};
        if (index + 1 < components.size()) parent.setPath(segment.absoluteFilePath());
    }
    const QFileInfo target(parent.filePath(components.last()));
    if (!target.isFile() || !safeFile(target))
        return {{}, {}, "Target is not an approved editable text file."};
    const QString actual = QDir::fromNativeSeparators(target.canonicalFilePath());
    const QString canonical = QDir::fromNativeSeparators(canonicalRoot);
#ifdef Q_OS_WIN
    const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
    if (!actual.startsWith(canonical.endsWith('/') ? canonical : canonical + '/',
                           pathCase))
        return {{}, {}, "Proposed file is outside the approved project."};
    return planExactReplace(actual, exactOldText, newText);
}
EditResult applyApprovedEdit(const EditPlan &plan,
                             const QString &backupDirectory) {
    EditResult result;
    QByteArray current;
    if (!plan.ok() || plan.path.isEmpty() ||
        plan.originalSha256.size() != 32 ||
        plan.originalBytes.size() > kMaximumFileBytes ||
        plan.proposedBytes.size() > kMaximumFileBytes ||
        !validUtf8(plan.proposedBytes) ||
        backupDirectory.isEmpty() || !exactSnapshot(plan, current)) {
        result.error = "Edit is stale or unsafe. Select and review the file again; "
                       "no code has been changed.";
        return result;
    }
    if (!QDir().mkpath(backupDirectory)) {
        result.error = "Could not create the private backup directory.";
        return result;
    }
    const QString backupName = QStringLiteral("%1-%2.bak")
        .arg(QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz"),
             QUuid::createUuid().toString(QUuid::WithoutBraces));
    result.backupPath = QDir(backupDirectory).filePath(backupName);
    QSaveFile backup(result.backupPath);
    backup.setDirectWriteFallback(false);
    if (!backup.open(QIODevice::WriteOnly) ||
        backup.write(plan.originalBytes) != plan.originalBytes.size() ||
        !backup.commit()) {
        result.error = "Could not write complete original-file backup. "
                       "Source unchanged.";
        result.backupPath.clear();
        return result;
    }
    // Recheck after the backup; reject changes made while the preview was open.
    if (!exactSnapshot(plan, current)) {
        result.error = "Source changed before approval was applied. "
                       "Backup retained; source untouched.";
        return result;
    }
    QSaveFile output(plan.path);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(plan.proposedBytes) != plan.proposedBytes.size() ||
        !output.commit()) {
        result.error = "Atomic source replacement failed. Original backup retained.";
        return result;
    }
    QFile verify(plan.path);
    if (!verify.open(QIODevice::ReadOnly) ||
        sha(verify.readAll()) != sha(plan.proposedBytes)) {
        result.error = "Post-write verification failed. Original backup retained.";
        return result;
    }
    return result;
}
}
