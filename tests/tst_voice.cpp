#include <QtTest>

#include "voice_intent.h"

using VoiceIntent::Intent;

// B2-9：语音确认的意图判定（纯逻辑，不用 Android / 麦克风）。
// 验收标准里的"10 次吃了≥9 次、10 次无关话 0 误确认"在逻辑层要做到
// 100%：识别文本明确命中才动作，疑问句/否定句一律不确认。
class TestVoice : public QObject
{
    Q_OBJECT

private slots:
    void confirmPhrases();
    void snoozePhrases();
    void irrelevantSpeechNeverConfirms();
    void questionsAndNegationsNeverConfirm();
    void punctuationAndFillers();
    void emptyAndNoise();
    void strictnessStats();
};

void TestVoice::confirmPhrases()
{
    const QStringList phrases {
        QStringLiteral("吃了"), QStringLiteral("吃过了"), QStringLiteral("吃了药"),
        QStringLiteral("喝完"), QStringLiteral("好了"), QStringLiteral("搞定了"),
        QStringLiteral("我吃了"), QStringLiteral("已经吃了"),
    };
    for (const QString &text : phrases)
        QCOMPARE(VoiceIntent::classify(text), Intent::Confirm);
}

void TestVoice::snoozePhrases()
{
    const QStringList phrases {
        QStringLiteral("等一下"), QStringLiteral("等一等"), QStringLiteral("待会儿"),
        QStringLiteral("稍后"), QStringLiteral("过会儿"), QStringLiteral("晚点再说"),
    };
    for (const QString &text : phrases)
        QCOMPARE(VoiceIntent::classify(text), Intent::Snooze);
}

void TestVoice::irrelevantSpeechNeverConfirms()
{
    // 验收标准：10 次无关话 0 次误确认。
    const QStringList phrases {
        QStringLiteral("今天天气不错"), QStringLiteral("我想出去走走"),
        QStringLiteral("电话响了"),     QStringLiteral("先看会儿电视"),
        QStringLiteral("水烧开了"),     QStringLiteral("这个药真苦"),
        QStringLiteral("谁来了"),       QStringLiteral("把窗关上"),
        QStringLiteral("我出去一下"),   QStringLiteral("稍等行程安排"),
    };
    for (const QString &text : phrases)
        QVERIFY2(VoiceIntent::classify(text) != Intent::Confirm,
                 qPrintable(QStringLiteral("无关话被误判为确认: ") + text));
}

void TestVoice::questionsAndNegationsNeverConfirm()
{
    // 疑问句和否定句哪怕含"吃"字，也不能触发确认。
    const QStringList phrases {
        QStringLiteral("吃了吗"),   QStringLiteral("吃了没有"),
        QStringLiteral("没吃"),     QStringLiteral("不吃"),
        QStringLiteral("还没吃"),   QStringLiteral("我不想吃"),
        QStringLiteral("要不要吃"), QStringLiteral("先别吃了"),
    };
    for (const QString &text : phrases)
        QVERIFY2(VoiceIntent::classify(text) != Intent::Confirm,
                 qPrintable(QStringLiteral("疑问/否定被误判为确认: ") + text));
}

void TestVoice::punctuationAndFillers()
{
    QCOMPARE(VoiceIntent::classify(QStringLiteral("吃了。")), Intent::Confirm);
    QCOMPARE(VoiceIntent::classify(QStringLiteral("吃 了！")), Intent::Confirm);
    QCOMPARE(VoiceIntent::classify(QStringLiteral("嗯，吃了呀")), Intent::Confirm);
    QCOMPARE(VoiceIntent::classify(QStringLiteral("那个……好了")), Intent::Confirm);
    QCOMPARE(VoiceIntent::classify(QStringLiteral("等一下啊")), Intent::Snooze);
    QCOMPARE(VoiceIntent::normalize(QStringLiteral("吃 了！")), QStringLiteral("吃了"));
}

void TestVoice::emptyAndNoise()
{
    QCOMPARE(VoiceIntent::classify(QString()), Intent::Unknown);
    QCOMPARE(VoiceIntent::classify(QStringLiteral("   ")), Intent::Unknown);
    QCOMPARE(VoiceIntent::classify(QStringLiteral("。！？，")), Intent::Unknown);
    QCOMPARE(VoiceIntent::name(Intent::Confirm), QStringLiteral("confirm"));
    QCOMPARE(VoiceIntent::name(Intent::Unknown), QStringLiteral("unknown"));
}

void TestVoice::strictnessStats()
{
    // 模拟验收数字：命中组全中，无关话组 0 误确认。
    int confirmed = 0;
    for (int i = 0; i < 10; ++i) {
        if (VoiceIntent::classify(QStringLiteral("吃了")) == Intent::Confirm)
            ++confirmed;
    }
    QVERIFY2(confirmed >= 9, "10 次'吃了'逻辑层应全部识别为确认");

    int falseConfirm = 0;
    for (int i = 0; i < 10; ++i) {
        if (VoiceIntent::classify(QStringLiteral("今天天气不错")) == Intent::Confirm)
            ++falseConfirm;
    }
    QCOMPARE(falseConfirm, 0);
}

QTEST_GUILESS_MAIN(TestVoice)
#include "tst_voice.moc"
