#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

namespace EmberLocalEdit {
// No automatic edits. This immutable-by-convention snapshot is produced by
// an explicit file picker and is applied only after an independent UI approval.
struct EditPlan {
    QString path;
    QString displayPreview;
    QString error;
    QByteArray originalSha256;
    QByteArray originalBytes;
    QByteArray proposedBytes;
    bool ok() const { return error.isEmpty(); }
};
struct SearchReplaceBlock {
    QString oldText;
    QString newText;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// Extract exactly one explicitly delimited edit suggestion from a model reply.
// Treat the model output as untrusted. The file path always comes from a
// separate native file picker and approval dialog.
SearchReplaceBlock parseSingleSearchReplaceBlock(const QString &modelReply);
struct EditResult {
    QString backupPath;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
EditPlan planExactReplace(const QString &selectedPath,
                          const QString &exactOldText,
                          const QString &newText);
// Model-submitted path MUST resolve strictly inside one user-selected
// canonical project root. Every component is checked (no links/traversal).
// This only stages an immutable proposal; it does not write to the source.
EditPlan planWithinProject(const QString &selectedRoot,
                           const QString &relativePath,
                           const QString &exactOldText,
                           const QString &newText);
EditResult applyApprovedEdit(const EditPlan &plan,
                             const QString &backupDirectory);

// Strictly create ONE new UTF-8 source file inside an explicitly selected
// existing project directory. The model can only stage; human approval of
// the entire new file is required. Apply uses NewOnly, never replaces.
// No directories are created and no external paths are accessible.
struct NewFilePlan {
    QString root, relativePath, path, displayPreview, error;
    QByteArray proposedBytes, sha256;
    bool ok() const { return error.isEmpty() && !path.isEmpty(); }
};
NewFilePlan planNewFileWithinProject(const QString &selectedRoot,
                                     const QString &relativePath,
                                     const QString &contents);
EditResult applyApprovedNewFile(const NewFilePlan &plan);
// A short human-reviewed batch of NEW source files: e.g. index.html,
// styles.css and app.js. No directory creation or overwrite is permitted.
struct NewFileSource { QString relativePath, content; };
struct NewFileBatchPlan {
    QVector<NewFilePlan> files;
    QString displayPreview, error;
    bool ok() const { return error.isEmpty() && !files.isEmpty(); }
};
struct NewFileBatchResult {
    QStringList createdPaths;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
NewFileBatchPlan planNewFilesWithinProject(
    const QString &selectedRoot, const QVector<NewFileSource> &files);
NewFileBatchResult applyApprovedNewFiles(const NewFileBatchPlan &batch);

// Explicitly approved, bounded 1-3 distinct existing UTF-8 source files.
// Planning only stages immutable snapshots; no source is written.
struct SourceReplacement { QString relativePath, oldText, newText; };
struct EditBatchPlan {
    QVector<EditPlan> edits;
    QString displayPreview, error;
    bool ok() const { return error.isEmpty() && !edits.isEmpty(); }
};
struct EditBatchResult {
    QStringList backupPaths;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
EditBatchPlan planWithinProjectBatch(
    const QString &selectedRoot, const QVector<SourceReplacement> &changes);
// Preflight all paths, back up every original, then QSaveFile each target.
// Failed multi-file writes attempt verified rollback. A power interruption
// is NOT cross-file atomic; retained backups allow manual recovery.
EditBatchResult applyApprovedBatch(
    const EditBatchPlan &batch, const QString &backupDirectory);
}
