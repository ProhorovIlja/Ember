#pragma once
#include <QString>

namespace EmberLocalFiles {
// A chosen file's contents are appended ONLY to the user-visible, editable
// composer. This is a foreground read; no directory scans, network, tool
// execution or automatic external requests occur.
struct ImportResult {
    QString draft;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
ImportResult importText(const QString &selectedPath, const QString &existingDraft);
}
