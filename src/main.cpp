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
            if (qEnvironmentVariableIsSet("YIBAN_DEMO")) {
                if (auto *device = engine.singletonInstance<QObject *>(
                        QStringLiteral("SmartGlasses"), QStringLiteral("DeviceVM"))) {
                    device->setProperty("demoMode",
                                        qEnvironmentVariable("YIBAN_DEMO") == QStringLiteral("1"));
                }
            }
            if (auto *vm = engine.singletonInstance<QObject *>(
                    QStringLiteral("SmartGlasses"), QStringLiteral("ReminderVM"))) {
                if (qEnvironmentVariableIsSet("YIBAN_ALL_DONE")) {
                    const QVariantList today = vm->property("today").toList();
                    for (const QVariant &entry : today) {
                        const QVariantMap occ = entry.toMap();
                        if (occ.value(QStringLiteral("state")).toString() != QStringLiteral("confirmed"))
                            QMetaObject::invokeMethod(vm, "confirm",
                                                      Q_ARG(qint64, occ.value(QStringLiteral("occurrenceId")).toLongLong()));
                    }
                }
            }
            if (const QByteArray page = qgetenv("YIBAN_PAGE"); !page.isEmpty()) {
                QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant,
                                                                     QString::fromLocal8Bit(page)));
            }
            if (qEnvironmentVariableIsSet("YIBAN_GATE_UNLOCK")) {
                // 仅截图回归：自动填入验证语并提交，检查缓冲页 -> 管理后台。
                QTimer::singleShot(600, window, [window] {
                    auto *gate = window->findChild<QObject *>(QStringLiteral("caregiverGate"));
                    if (!gate)
                        return;
                    if (auto *input = gate->findChild<QObject *>(QStringLiteral("phraseInput")))
                        input->setProperty("text", QStringLiteral("我是看护人"));
                    QMetaObject::invokeMethod(gate, "checkPhrase");
                });
            }
            if (qEnvironmentVariableIsSet("YIBAN_SCROLL_DEMO")) {
                // 仅截图回归：把设置页滚到演示模式卡。
                QTimer::singleShot(700, window, [window] {
                    auto *scroll = window->findChild<QObject *>(QStringLiteral("settingsScroll"));
                    auto *card = window->findChild<QObject *>(QStringLiteral("demoModeCard"));
                    if (scroll && card) {
                        scroll->setProperty("contentY", qMax<qreal>(
                            0, card->property("y").toReal() - 120));
                    }
                });
            }
            if (qEnvironmentVariableIsSet("YIBAN_SCROLL_END")) {
                // 仅截图回归：把设置页滚到底，检查看护人入口。
                QTimer::singleShot(700, window, [window] {
                    auto *scroll = window->findChild<QObject *>(QStringLiteral("settingsScroll"));
                    if (!scroll)
                        scroll = window->findChild<QObject *>(QStringLiteral("adminScroll"));
                    if (scroll) {
                        const qreal bottom = scroll->property("contentHeight").toReal()
                                             - scroll->property("height").toReal();
                        scroll->setProperty("contentY", qMax<qreal>(0, bottom));
                    }
                });
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
