#include "EmberBackend.h"
#include <QApplication>
#include <QCoreApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Ember Local");
    QCoreApplication::setApplicationName("Ember");
    QQmlApplicationEngine engine;
    EmberBackend backend;
    engine.rootContext()->setContextProperty("ember", &backend);
    engine.loadFromModule("Ember", "Main");
    return engine.rootObjects().isEmpty() ? 1 : app.exec();
}
