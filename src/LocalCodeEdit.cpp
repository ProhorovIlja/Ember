#include "LocalCodeEdit.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
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

NewFilePlan planNewFileWithinProject(const QString &selectedRoot,
                                     const QString &relativePath,
                                     const QString &contents) {
    NewFilePlan result;
    // Deliberately smaller than the model's general patch budget. This is
    // one new source file, never a project generator or shell command.
    if (contents.isEmpty() || contents.size() > 8000 ||
        contents.contains(QChar::Null) ||
        relativePath.isEmpty() || relativePath.size() > 240 ||
        relativePath.startsWith('/') || relativePath.endsWith('/') ||
        relativePath.contains('\\') || relativePath.contains(':') ||
        relativePath.contains("//") || relativePath.contains('\r') ||
        relativePath.contains('\n') || relativePath.contains(QChar::Null)) {
        result.error = "New file needs a valid project-relative path and up "
                       "to 8000 characters of source content.";
        return result;
    }
    const QByteArray bytes = contents.toUtf8();
    if (!validUtf8(bytes) || bytes.size() > 16000) {
        result.error = "New source file must contain plain valid UTF-8 text.";
        return result;
    }
    const QFileInfo rootInfo(selectedRoot);
    if (!rootInfo.exists() || !rootInfo.isDir() ||
        rootInfo.isSymLink() || rootInfo.isJunction()) {
        result.error = "Select a real existing project directory.";
        return result;
    }
    result.root = rootInfo.canonicalFilePath();
    if (result.root.isEmpty()) {
        result.error = "Cannot resolve the selected project directory.";
        return result;
    }
    static const QSet<QString> blockedDirs{
        ".git", ".hg", ".svn", ".idea", ".vs", ".vscode",
        "venv", ".venv", "node_modules", "__pycache__", "cache",
        "build", "dist", "bin", "obj", "target", "vendor"
    };
    const QStringList parts = relativePath.split('/');
    QDir parent(result.root);
    for (int i = 0; i < parts.size(); ++i) {
        const QString part = parts.at(i);
        if (part.isEmpty() || part == "." || part == ".." ||
            part.startsWith('.') || part.endsWith('.') ||
            part.endsWith(' ') || blockedDirs.contains(part.toLower()) ||
            part.contains(QRegularExpression(R"([<>\"|?*])"))) {
            result.error = "Hidden, reserved, traversal or invalid file path denied.";
            return result;
        }
        const QString stem = part.section('.', 0, 0).toUpper();
        static const QSet<QString> reserved{
            "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3",
            "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
            "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6",
            "LPT7", "LPT8", "LPT9"
        };
        if (reserved.contains(stem)) {
            result.error = "Windows device filenames are not project files.";
            return result;
        }
        if (i + 1 < parts.size()) {
            const QFileInfo directory(parent.filePath(part));
            if (!directory.exists() || !directory.isDir() ||
                directory.isSymLink() || directory.isJunction()) {
                result.error = "Parent directory must already exist, without links.";
                return result;
            }
            const QString canonical = directory.canonicalFilePath();
            if (canonical.isEmpty()) {
                result.error = "Cannot resolve a proposed source parent directory.";
                return result;
            }
            parent.setPath(canonical);
        }
    }
    const QString fileName = parts.last();
    const QFileInfo nameInfo(fileName);
    if (!kAllowedExtensions.contains(nameInfo.suffix().toLower()) &&
        fileName.toLower() != "makefile" &&
        fileName.toLower() != "dockerfile") {
        result.error = "Creating executables, secrets or unsupported files is denied.";
        return result;
    }
    const QFileInfo candidate(parent.filePath(fileName));
    if (candidate.exists() || candidate.isSymLink() || candidate.isJunction()) {
        result.error = "Target already exists. New files cannot overwrite.";
        return result;
    }
    const QString canonicalRoot = QDir::fromNativeSeparators(result.root);
    const QString canonicalParent = QDir::fromNativeSeparators(
        QFileInfo(parent.path()).canonicalFilePath());
#ifdef Q_OS_WIN
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    if (canonicalParent.compare(canonicalRoot, pathCase) != 0 &&
        !canonicalParent.startsWith(
            canonicalRoot.endsWith('/') ? canonicalRoot :
            canonicalRoot + '/', pathCase)) {
        result.error = "New source file would escape the selected project.";
        return result;
    }
    result.relativePath = relativePath;
    result.path = parent.filePath(fileName);
    result.proposedBytes = bytes;
    result.sha256 = sha(bytes);
    // A complete preview, with no truncation or hidden generated lines.
    result.displayPreview =
        QString("NEW FILE (NOT WRITTEN): %1\n"
                "Proposed UTF-8 bytes: %2\n"
                "Proposed SHA-256: %3\n"
                "Existing files will not be overwritten.\n"
                "No directories, tests or commands will be run.\n\n"
                "===== FULL NEW FILE CONTENT =====\n%4\n"
                "===== END NEW FILE =====\n"
                "Review this entire source before applying.")
            .arg(result.path, QString::number(bytes.size()),
                 QString::fromLatin1(result.sha256.toHex()), contents);
    return result;
}
EditResult applyApprovedNewFile(const NewFilePlan &plan) {
    EditResult result;
    if (!plan.ok() || plan.sha256.size() != 32 ||
        sha(plan.proposedBytes) != plan.sha256 ||
        !validUtf8(plan.proposedBytes)) {
        result.error = "Invalid staged new-file proposal; nothing created.";
        return result;
    }
    // Revalidate ALL path components at the approval boundary. Fail closed
    // when the target or a parent was replaced after preview generation.
    const NewFilePlan fresh = planNewFileWithinProject(
        plan.root, plan.relativePath, QString::fromUtf8(plan.proposedBytes));
    if (!fresh.ok() || fresh.path != plan.path ||
        fresh.sha256 != plan.sha256 || fresh.proposedBytes != plan.proposedBytes) {
        result.error = "New-file path changed or was created after review; "
                       "no file was overwritten.";
        return result;
    }
    QFile output(fresh.path);
    // NewOnly is mandatory: QSaveFile would REPLACE an existing target.
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        result.error = "Could not create a new file exclusively; target untouched.";
        return result;
    }
    const bool complete = output.write(fresh.proposedBytes) ==
                              fresh.proposedBytes.size() &&
                          output.flush() && output.error() == QFileDevice::NoError;
    output.close();
    QFile check(fresh.path);
    const bool verified = complete && check.open(QIODevice::ReadOnly) &&
        sha(check.readAll()) == fresh.sha256;
    check.close();
    if (!verified) {
        // Remove only the bytes WE wrote, avoiding deletion of a concurrently
        // changed file. A crash could still leave a partial new file.
        QFile cleanup(fresh.path);
        if (cleanup.open(QIODevice::ReadOnly) &&
            cleanup.readAll() == fresh.proposedBytes) {
            cleanup.close();
            QFile::remove(fresh.path);
        }
        result.error = "New-file write/verification failed. Inspect the "
                       "target path for a partial file before retrying.";
    }
    return result;
}

EditBatchPlan planWithinProjectBatch(
    const QString &selectedRoot, const QVector<SourceReplacement> &changes) {
    EditBatchPlan batch;
    if (changes.isEmpty() || changes.size() > 3) {
        batch.error = "A source batch requires one to three distinct files.";
        return batch;
    }
    QSet<QString> seen;
    QStringList previews;
    for (const auto &change : changes) {
        EditPlan edit = planWithinProject(
            selectedRoot, change.relativePath, change.oldText, change.newText);
        if (!edit.ok()) {
            batch.error = "Batch refused for " + change.relativePath +
                          ": " + edit.error;
            return batch;
        }
        const QString key = QDir::fromNativeSeparators(edit.path).toCaseFolded();
        if (seen.contains(key)) {
            batch.error = "A batch cannot edit the same source file twice.";
            return batch;
        }
        seen.insert(key);
        previews << QString("CHANGE %1 OF %2\n%3")
            .arg(batch.edits.size() + 1).arg(changes.size())
            .arg(edit.displayPreview);
        batch.edits.append(std::move(edit));
    }
    batch.displayPreview = previews.join(
        "\n\n========== NEXT SOURCE FILE ==========\n\n");
    return batch;
}
EditBatchResult applyApprovedBatch(
    const EditBatchPlan &batch, const QString &backupDirectory) {
    EditBatchResult result;
    if (!batch.ok() || batch.edits.size() > 3 || backupDirectory.isEmpty()) {
        result.error = "No valid approved multi-file edit is ready.";
        return result;
    }
    QSet<QString> paths;
    QByteArray current;
    // Before making any source changes, verify all immutable snapshots.
    for (const auto &edit : batch.edits) {
        const QString key = QDir::fromNativeSeparators(edit.path).toCaseFolded();
        if (paths.contains(key) || edit.originalSha256.size() != 32 ||
            !validUtf8(edit.proposedBytes) || !exactSnapshot(edit, current)) {
            result.error = "A batch source changed or is unsafe. Nothing modified.";
            return result;
        }
        paths.insert(key);
    }
    if (!QDir().mkpath(backupDirectory)) {
        result.error = "Could not create private backup directory.";
        return result;
    }
    for (const auto &edit : batch.edits) {
        const QString backupPath = QDir(backupDirectory).filePath(
            QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz") +
            "-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".bak");
        QSaveFile backup(backupPath);
        backup.setDirectWriteFallback(false);
        if (!backup.open(QIODevice::WriteOnly) ||
            backup.write(edit.originalBytes) != edit.originalBytes.size() ||
            !backup.commit()) {
            result.error = "Cannot back up all files. Sources untouched; any "
                           "completed private backups were retained.";
            return result;
        }
        result.backupPaths.append(backupPath);
    }
    for (const auto &edit : batch.edits) {
        if (!exactSnapshot(edit, current)) {
            result.error = "A source changed during backup. Sources untouched.";
            return result;
        }
    }
    qsizetype applied = 0;
    QString failure;
    for (const auto &edit : batch.edits) {
        if (!exactSnapshot(edit, current)) {
            failure = "Source changed during batch application.";
            break;
        }
        QSaveFile output(edit.path);
        output.setDirectWriteFallback(false);
        if (!output.open(QIODevice::WriteOnly) ||
            output.write(edit.proposedBytes) != edit.proposedBytes.size() ||
            !output.commit()) {
            failure = "Cannot atomically replace one batch source.";
            break;
        }
        ++applied;
        QFile verify(edit.path);
        if (!verify.open(QIODevice::ReadOnly) ||
            sha(verify.readAll()) != sha(edit.proposedBytes)) {
            failure = "Post-write SHA-256 verification failed.";
            break;
        }
    }
    if (failure.isEmpty()) return result;
    bool rollbackIncomplete = false;
    for (qsizetype i = applied; i > 0; --i) {
        const auto &edit = batch.edits.at(i - 1);
        QFile currentFile(edit.path);
        if (!currentFile.open(QIODevice::ReadOnly) ||
            currentFile.readAll() != edit.proposedBytes) {
            rollbackIncomplete = true; // never overwrite a concurrent user edit
            continue;
        }
        currentFile.close();
        QSaveFile rollback(edit.path);
        rollback.setDirectWriteFallback(false);
        if (!rollback.open(QIODevice::WriteOnly) ||
            rollback.write(edit.originalBytes) != edit.originalBytes.size() ||
            !rollback.commit()) {
            rollbackIncomplete = true;
        }
    }
    result.error = failure +
        (rollbackIncomplete
            ? " ROLLBACK INCOMPLETE: restore from the retained backups manually."
            : " Previous files rolled back; private backups retained.");
    return result;
}
}
