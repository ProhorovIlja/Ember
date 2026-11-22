#include "ProjectTestRun.h"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace EmberProjectTests {
namespace {
bool regular(const QString &path) {
    const QFileInfo info(path);
    return info.isFile() && info.isReadable() &&
           !info.isSymLink() && !info.isJunction();
}
bool directory(const QString &path) {
    const QFileInfo info(path);
    return info.isDir() && info.isReadable() &&
           !info.isSymLink() && !info.isJunction();
}
}
Plan prepare(const QString &selectedRoot, Preset preset) {
    const QFileInfo rootInfo(selectedRoot);
    if (!directory(selectedRoot) || rootInfo.canonicalFilePath().isEmpty())
        return {{}, {}, {}, {}, "Select a real project directory, not a link."};
    Plan p;
    p.workingDirectory = rootInfo.canonicalFilePath();
    const QDir root(p.workingDirectory);
    QString executable;
    switch (preset) {
    case Preset::CTest: {
        const QString build = root.filePath("build");
        if (!directory(build) ||
            !regular(QDir(build).filePath("CTestTestfile.cmake")))
            return {{}, {}, {}, {}, "CTest needs a real build/ directory "
                                      "containing CTestTestfile.cmake."};
        executable = "ctest";
        p.arguments = {"--test-dir", QFileInfo(build).canonicalFilePath(),
                       "-C", "Release", "--output-on-failure"};
        p.label = "CTest · Release";
        break;
    }
    case Preset::Cargo:
        if (!regular(root.filePath("Cargo.toml")))
            return {{}, {}, {}, {}, "Cargo needs a regular Cargo.toml file."};
        executable = "cargo";
        p.arguments = {"test", "--locked"};
        p.label = "Cargo test --locked";
        break;
    case Preset::PythonUnittest:
        if (!directory(root.filePath("tests")))
            return {{}, {}, {}, {}, "Python unittest needs a real tests/ folder."};
        executable = "python";
        p.arguments = {"-m", "unittest", "discover", "-v"};
        p.label = "Python unittest discover";
        break;
    default:
        return {{}, {}, {}, {}, "Unsupported project test preset."};
    }
    p.program = QStandardPaths::findExecutable(executable);
    if (p.program.isEmpty() || !regular(p.program))
        return {{}, {}, {}, {}, "Required test executable not found in PATH."};
    return p;
}
}
