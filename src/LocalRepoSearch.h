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
// Read up to 65 numbered lines from one selected UTF-8 source file.
// Never follow symlinks or escape the user's chosen canonical project root.
SearchResult readProjectFile(const QString &selectedRoot,
                             const QString &relativePath, int startLine = 1);
}
