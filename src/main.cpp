#include "EmberBackend.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Ember Local");
    QCoreApplication::setApplicationName("Ember");

    const bool smokeTest = app.arguments().contains("--smoke-test");
    QQmlApplicationEngine engine;
    bool qmlWarnings = false;
    if (smokeTest) {
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app,
                         [&](const QList<QQmlError> &warnings) {
            for (const auto &warning : warnings) qWarning().noquote() << warning.toString();
            qmlWarnings = qmlWarnings || !warnings.isEmpty();
        });
    }
    EmberBackend backend;
    engine.rootContext()->setContextProperty("ember", &backend);
    engine.loadFromModule("Ember", "Main");
    if (engine.rootObjects().isEmpty() || qmlWarnings) return 1;
    if (smokeTest) {
        // Render the real QML window once on the software/offscreen backend.
        // This does not infer working voice models, microphone or full visual QA.
        QTimer::singleShot(1200, &app, &QCoreApplication::quit);
    }
    return app.exec();
}
