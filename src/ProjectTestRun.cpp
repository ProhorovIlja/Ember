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
    case Preset::NodeBuiltIn:
        if (!regular(root.filePath("package.json")))
            return {{}, {}, {}, {}, "Node.js tests need a regular package.json."};
        // Use node.exe directly, never npm.cmd, package scripts or a shell.
        executable = "node";
        p.arguments = {"--test"};
        p.label = "Node.js built-in tests";
        break;
    case Preset::GoTest:
        if (!regular(root.filePath("go.mod")))
            return {{}, {}, {}, {}, "Go tests need a regular go.mod."};
        executable = "go";
        p.arguments = {"test", "./..."};
        p.label = "Go test ./...";
        break;
    case Preset::PythonPytest:
        if (!directory(root.filePath("tests")) &&
            !regular(root.filePath("pytest.ini")) &&
            !regular(root.filePath("pyproject.toml")))
            return {{}, {}, {}, {}, "pytest needs a tests/ folder, pytest.ini or pyproject.toml."};
        executable = "python";
        p.arguments = {"-m", "pytest", "-q"};
        p.label = "Python pytest -q";
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
