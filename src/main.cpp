#include "EmberBackend.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QList>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QResource>
#include <QQuickStyle>
#include <QStringList>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Ember Local");
    QCoreApplication::setApplicationName("Ember");
    app.setWindowIcon(QIcon(":/qt/qml/Ember/assets/ember-emblem.svg"));

    // The Windows native Qt style rejects customized backgrounds.
    // Basic preserves the original Ember design without a web-renderer.
    QQuickStyle::setStyle("Basic");
    const bool smokeTest = app.arguments().contains("--smoke-test");
    // CI and installer smoke runs must NEVER seed a portable/installed app's
    // data/, cache/, SQLite or image Library before wildcard packaging.
    // A private temporary root exists only for this foreground smoke run.
    std::unique_ptr<QTemporaryDir> smokeStorage;
    if (smokeTest) {
        smokeStorage = std::make_unique<QTemporaryDir>(
            QDir(QDir::tempPath()).filePath("ember-smoke-profile-XXXXXX"));
        if (!smokeStorage->isValid()) {
            qCritical("Failed to create isolated QML smoke-test profile.");
            return 5;
        }
        qputenv("EMBER_INTERNAL_SMOKE_ROOT", smokeStorage->path().toUtf8());
    }
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
        const QString report = phase + "\nRoot objects: " +
            QString::number(engine.rootObjects().size()) + "\n" +
            qmlDiagnostics.join("\n") + "\n";
        // A report path is provided by the explicit foreground CI smoke test.
        // Do not write diagnostic files anywhere under the deployed app.
        const QString reportFile = qEnvironmentVariable("EMBER_SMOKE_REPORT_PATH");
        if (!reportFile.isEmpty()) {
            QFile log(reportFile);
            if (log.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
                log.write(report.toUtf8());
        }
        qInfo().noquote() << report;
    };
    if (engine.rootObjects().isEmpty() || !qmlDiagnostics.isEmpty()) {
        writeDiagnostics("QML initialization failed");
        return 1;
    }
    if (smokeTest && !backend.localHistoryReady()) {
        writeDiagnostics("SQLite driver or writable private data folder unavailable");
        return 3;
    }
    if (smokeTest &&
        !QResource(":/qt/qml/Ember/assets/ember-emblem.svg").isValid()) {
        writeDiagnostics("Original Ember icon asset was not embedded");
        return 4;
    }
    if (smokeTest) {
        // Single, foreground, model-free QML smoke: verify all native routes.
        // Existing CI calls --smoke-test explicitly; no new background task.
        const QList<int> pages{0, 1, 2, 3};
        for (int i = 0; i < pages.size(); ++i) {
            // Capture the integer by VALUE; pages goes out of scope before
            // the event loop executes these foreground smoke callbacks.
            QTimer::singleShot(100 + 170 * i, &app, [&, page = pages.at(i)] {
                QObject *root = engine.rootObjects().first();
                QObject *stack = root->findChild<QObject *>("emberViews");
                QObject *voiceNav = root->findChild<QObject *>("emberVoiceNav");
                if (!voiceNav || !stack || !root->setProperty("page", page) ||
                    stack->property("currentIndex").toInt() != page) {
                    qmlDiagnostics.append(
                        QString("Native QML page navigation failed for %1").arg(page));
                }
            });
        }
        QTimer::singleShot(980, &app, [&] {
            if (!engine.rootObjects().isEmpty())
                engine.rootObjects().first()->setProperty("page", 1);
            writeDiagnostics("QML initialized; Voice, Chat, Settings and Library checked");
            app.quit();
        });
    }
    const int result = app.exec();
    if (smokeTest) writeDiagnostics("QML event loop exited");
    return smokeTest && !qmlDiagnostics.isEmpty() ? 2 : result;
}
