#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QCoreApplication::setOrganizationName("Yiban");
    QCoreApplication::setApplicationName("YibanElder");

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "yiban_android_app_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            app.installTranslator(&translator);
            break;
        }
    }

    QQmlApplicationEngine engine;
    engine.loadFromModule("SmartGlasses", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
