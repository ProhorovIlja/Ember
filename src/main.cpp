#include "EmberBackend.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringList>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Ember Local");
    QCoreApplication::setApplicationName("Ember");

    const bool smokeTest = app.arguments().contains("--smoke-test");
    QQmlApplicationEngine engine;
    QStringList qmlDiagnostics;
    if (smokeTest) {
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                         [&](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings) {
                qmlDiagnostics.append(warning.toString());
            }
        });
    }
    EmberBackend backend;
    engine.rootContext()->setContextProperty("ember", &backend);
    engine.loadFromModule("Ember", "Main");
    auto writeDiagnostics = [&](const QString &phase) {
        if (!smokeTest) return;
        QFile log(QCoreApplication::applicationDirPath() + "/qml-smoke-report.txt");
        if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            const QString report = phase + "\nRoot objects: " +
                QString::number(engine.rootObjects().size()) + "\n" +
                qmlDiagnostics.join("\n") + "\n";
            log.write(report.toUtf8());
            log.close();
        }
    };
    if (engine.rootObjects().isEmpty() || !qmlDiagnostics.isEmpty()) {
        writeDiagnostics("QML initialization failed");
        return 1;
    }
    if (smokeTest) {
        writeDiagnostics("QML initialized");
        QTimer::singleShot(1200, &app, &QCoreApplication::quit);
    }
    const int result = app.exec();
    if (smokeTest) writeDiagnostics("QML event loop exited");
    return smokeTest && !qmlDiagnostics.isEmpty() ? 2 : result;
}
