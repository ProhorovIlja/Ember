#pragma once
#include <QByteArray>
#include <QString>

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
EditResult applyApprovedEdit(const EditPlan &plan,
                             const QString &backupDirectory);
}
