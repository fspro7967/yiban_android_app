#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>

#ifdef YIBAN_DEV_SHOTS
#include <QImage>
#include <QQuickWindow>
#include <QTimer>
#endif

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

#ifdef YIBAN_DEV_SHOTS
    // 仅开发构建：启动后抓一张窗口图再退出，用于 CI/文档截图。
    // 用法：YIBAN_SHOT=/tmp/home.png YIBAN_SHOT_DELAY=1500 ./yiban_android_app
    if (const QByteArray shot = qgetenv("YIBAN_SHOT"); !shot.isEmpty()) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
        const int delay = qEnvironmentVariableIntValue("YIBAN_SHOT_DELAY");
        if (window) {
            if (const QByteArray page = qgetenv("YIBAN_PAGE"); !page.isEmpty()) {
                QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant,
                                                                     QString::fromLocal8Bit(page)));
            }
            const QByteArray overlay = qgetenv("YIBAN_SHOT_OVERLAY");
            const int settle = delay > 0 ? delay : 1200;
            QTimer::singleShot(settle, window, [window, &engine, shot, overlay] {
                if (overlay == "reminder") {
                    // 拍全屏到点提醒：借演示口子立刻弹一条。
                    if (auto *vm = engine.singletonInstance<QObject *>(
                            QStringLiteral("SmartGlasses"), QStringLiteral("ReminderVM")))
                        QMetaObject::invokeMethod(vm, "triggerDemoReminder");
                } else if (overlay == "call") {
                    // 拍呼救倒计时：走 SosButton 同一条入口。
                    QMetaObject::invokeMethod(window, "startSos");
                } else {
                    if (auto *reminder = window->findChild<QObject *>(QStringLiteral("reminderScreen")))
                        QMetaObject::invokeMethod(reminder, "close");
                    if (auto *call = window->findChild<QObject *>(QStringLiteral("callScreen")))
                        QMetaObject::invokeMethod(call, "cancel");
                }
                QTimer::singleShot(overlay.isEmpty() ? 250 : 900, window, [window, shot] {
                    window->grabWindow().save(QString::fromLocal8Bit(shot));
                    QCoreApplication::quit();
                });
            });
        }
    }
#endif

    return app.exec();
}
