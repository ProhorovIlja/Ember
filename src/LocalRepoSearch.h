#pragma once
#include <QString>

namespace EmberLocalSearch {
struct SearchResult {
    QString draft;
    QString error;
    int matches = 0;
    bool limited = false;
    bool ok() const { return error.isEmpty(); }
};
// Explicit, synchronous, bounded search in a user-selected directory. Never
// called by an LLM; source text becomes an editable user preview only.
SearchResult searchDirectory(const QString &selectedRoot, const QString &literalNeedle,
                             const QString &existingDraft);
// One-shot, filename-only map of a user-selected repository. Never scans file
// contents, follows symlinks, or submits data without an editable preview.
SearchResult outlineDirectory(const QString &selectedRoot,
                              const QString &existingDraft);
}
