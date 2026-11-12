#include "profile_vm.h"

#include "app_core.h"

#include <QRegularExpression>

ProfileVM::ProfileVM(QObject *parent)
    : QObject(parent)
{
    connect(AppCore::instance(), &AppCore::profileChanged, this, &ProfileVM::profileChanged);
}

QString ProfileVM::elderName() const
{
    return AppCore::instance()->elderName();
}

QString ProfileVM::caregiverName() const
{
    return AppCore::instance()->caregiverName();
}

QString ProfileVM::caregiverPhone() const
{
    return AppCore::instance()->caregiverPhone();
}

void ProfileVM::applyProfile(const QString &elderName, const QString &caregiverName,
                             const QString &caregiverPhone)
{
    AppCore::instance()->setProfile(elderName, caregiverName, caregiverPhone);
}

namespace {

// 去掉用户常敲的分隔符，只留数字和开头的 "+"。
QString compactPhone(const QString &phone)
{
    QString out;
    const QString trimmed = phone.trimmed();
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar c = trimmed.at(i);
        if (c.isDigit())
            out.append(c);
        else if (c == QLatin1Char('+') && out.isEmpty())
            out.append(c);
    }
    return out;
}

// 去掉国家码，返回国内号码；不是 +86 的国际号码返回空。
QString domesticPart(const QString &compact)
{
    if (compact.startsWith(QStringLiteral("+86")))
        return compact.mid(3);
    if (compact.startsWith(QStringLiteral("0086")))
        return compact.mid(4);
    if (compact.startsWith(QLatin1Char('+')))
        return QString();
    if (compact.startsWith(QStringLiteral("86")) && compact.size() == 13)
        return compact.mid(2);
    return compact;
}

const QRegularExpression &mobilePattern()
{
    static const QRegularExpression re(QStringLiteral("^1[3-9]\\d{9}$"));
    return re;
}

// 固话：0 + 2~3 位区号 + 7~8 位号码。区号 010、02x 是 3 位（含 0），其余 4 位。
const QRegularExpression &landlinePattern()
{
    static const QRegularExpression re(QStringLiteral("^(0(?:10|2\\d|[3-9]\\d{2}))([1-9]\\d{6,7})$"));
    return re;
}

} // namespace

QString ProfileVM::phoneProblem(const QString &phone) const
{
    const QString trimmed = phone.trimmed();
    if (trimmed.isEmpty())
        return QStringLiteral("请填写电话号码");

    for (const QChar c : trimmed) {
        if (!c.isDigit() && !QStringLiteral(" +-()（）").contains(c))
            return QStringLiteral("电话只能包含数字，可以带空格、+、-");
    }

    const QString compact = compactPhone(trimmed);
    if (compact.startsWith(QLatin1Char('+')) && !compact.startsWith(QStringLiteral("+86")))
        return QStringLiteral("目前只支持中国大陆号码（+86）");

    const QString local = domesticPart(compact);
    if (mobilePattern().match(local).hasMatch() || landlinePattern().match(local).hasMatch())
        return QString();

    if (local.startsWith(QLatin1Char('1'))) {
        if (local.size() != 11)
            return QStringLiteral("手机号应为 11 位，现在是 %1 位").arg(local.size());
        return QStringLiteral("手机号第二位应为 3–9，请检查是否输错");
    }
    if (local.startsWith(QLatin1Char('0')))
        return QStringLiteral("固定电话请写成“区号 + 号码”，例如 020-1234 5678");
    return QStringLiteral("请填写 11 位手机号，或带区号的固定电话");
}

QString ProfileVM::formatPhone(const QString &phone) const
{
    if (!phoneProblem(phone).isEmpty())
        return phone.trimmed();

    const QString local = domesticPart(compactPhone(phone));
    if (mobilePattern().match(local).hasMatch())
        return QStringLiteral("%1 %2 %3").arg(local.left(3), local.mid(3, 4), local.mid(7));

    const QRegularExpressionMatch m = landlinePattern().match(local);
    const QString number = m.captured(2);
    const QString grouped = number.size() == 8
        ? number.left(4) + QLatin1Char(' ') + number.mid(4)
        : number.left(3) + QLatin1Char(' ') + number.mid(3);
    return m.captured(1) + QLatin1Char('-') + grouped;
}

bool ProfileVM::updateCaregiver(const QString &name, const QString &phone)
{
    const QString trimmedName = name.trimmed();
    if (trimmedName.isEmpty() || !phoneProblem(phone).isEmpty())
        return false;

    AppCore::instance()->setProfile(AppCore::instance()->elderName(), trimmedName,
                                    formatPhone(phone));
    return true;
}
