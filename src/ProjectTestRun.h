#pragma once
#include <QString>
#include <QStringList>

namespace EmberProjectTests {
enum class Preset {
    CTest = 0, Cargo = 1, PythonUnittest = 2,
    NodeBuiltIn = 3, GoTest = 4, PythonPytest = 5
};
struct Plan {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QString label;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// Pure planning only. No process is started, and no model can provide
// arbitrary shell strings, paths outside the chosen root or environment.
Plan prepare(const QString &selectedRoot, Preset preset);
}
