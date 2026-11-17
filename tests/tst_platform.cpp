#include <QtTest>

#include <QDateTime>
#include <QSettings>
#include <QTemporaryDir>

#include "app_settings.h"
#include "platform.h"

// B2 的平台层与设置持久化单测（桌面可跑，不需要 Android 设备）。
//
// 锁三件事：
//   1. 桌面构建下平台层必须是"安全空实现"——否则 QML 和截图脚本在 PC 上会崩；
//   2. 设置的读写要能跨"进程重启"（这里用重新构造 QSettings 模拟）；
//   3. device_id 生成一次之后不能再变（配对和 /v1/alarm 都认它）。
//
// Android 上的真机行为（通知能不能弹、闹钟准不准）不在这里测，
// 那是 W5 设备矩阵的事，测不了就别假装测了。

class PlatformTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void desktopPlatformIsNoop();
    void settingsRoundTrip();
    void deviceIdIsStable();
    void pairingStatePersists();

private:
    QTemporaryDir m_dir;
};

void PlatformTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    // 把 QSettings 指到临时目录，别污染开发机的注册表/家目录
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    QCoreApplication::setOrganizationName(QStringLiteral("YibanTest"));
    QCoreApplication::setApplicationName(QStringLiteral("YibanElderTest"));

    AppSettings::instance()->reset();
}

void PlatformTest::desktopPlatformIsNoop()
{
    QVERIFY(!Platform::isAndroid());

    // demo 口径：三档权限全给，QML 不需要为桌面写分支
    const QStringList labels = Platform::grantedLabels();
    QCOMPARE(int(labels.size()), 3);
    QVERIFY(labels.contains(QStringLiteral("通知")));

    // 系统动作一律"做不到"，但绝不抛异常、绝不崩
    QVERIFY(!Platform::hasPermission(Platform::Permission::Notifications));
    QVERIFY(!Platform::scheduleAlarm(1, QDateTime::currentMSecsSinceEpoch() + 60000,
                                     QStringLiteral("吃药"), QStringLiteral("2 片")));
    QVERIFY(!Platform::call(QStringLiteral("13800005678")));
    QVERIFY(!Platform::sendSms(QStringLiteral("13800005678"), QStringLiteral("test")));
    QVERIFY(!Platform::enterKiosk());

    // 这些是纯通知/取消，桌面下就是空操作：调用不该有任何副作用或崩溃
    Platform::notify(1, QStringLiteral("标题"), QStringLiteral("正文"), true);
    Platform::cancelNotification(1);
    Platform::cancelAlarm(1);
    Platform::cancelAllAlarms();
    Platform::exitKiosk();
    Platform::requestPermission(Platform::Permission::Notifications);
    Platform::requestIgnoreBatteryOptimizations();
}

void PlatformTest::settingsRoundTrip()
{
    AppSettings *s = AppSettings::instance();

    s->setServerUrl(QStringLiteral("https://example.org/v1"));
    s->setVoiceEnabled(false);
    s->setNightQuiet(false);
    s->setLastSyncTs(1793980800000LL);

    // 重新读一遍（相当于重启后重新打开应用）
    QCOMPARE(AppSettings::instance()->serverUrl(), QStringLiteral("https://example.org/v1"));
    QCOMPARE(AppSettings::instance()->voiceEnabled(), false);
    QCOMPARE(AppSettings::instance()->nightQuiet(), false);
    QCOMPARE(AppSettings::instance()->lastSyncTs(), 1793980800000LL);

    // 默认值是"语音开、夜间安静开"——与 DeviceVM 的默认一致
    AppSettings::instance()->reset();
    QCOMPARE(AppSettings::instance()->voiceEnabled(), true);
    QCOMPARE(AppSettings::instance()->nightQuiet(), true);
    QCOMPARE(AppSettings::instance()->serverUrl(), QStringLiteral("https://api.yiban.example/v1"));
}

void PlatformTest::deviceIdIsStable()
{
    const QString first = AppSettings::instance()->deviceId();
    QVERIFY(!first.isEmpty());
    QCOMPARE(AppSettings::instance()->deviceId(), first);

    // 清空后重新生成，必须是另一个（否则等于所有设备共用一个 id）
    AppSettings::instance()->reset();
    QVERIFY(AppSettings::instance()->deviceId() != first);
}

void PlatformTest::pairingStatePersists()
{
    AppSettings *s = AppSettings::instance();
    QCOMPARE(s->paired(), false);

    s->setToken(QStringLiteral("token-abc"));
    s->setPairing(true, QStringLiteral("caregiver"));

    QCOMPARE(AppSettings::instance()->paired(), true);
    QCOMPARE(AppSettings::instance()->role(), QStringLiteral("caregiver"));
    QCOMPARE(AppSettings::instance()->token(), QStringLiteral("token-abc"));

    // 取消配对必须是同步清 token：否则下次启动会带着失效的 token 反复 401
    s->setPairing(false, QStringLiteral("elder"));
    s->clearToken();
    QCOMPARE(AppSettings::instance()->paired(), false);
    QCOMPARE(AppSettings::instance()->role(), QStringLiteral("elder"));
    QVERIFY(AppSettings::instance()->token().isEmpty());
}

QTEST_GUILESS_MAIN(PlatformTest)

#include "tst_platform.moc"
