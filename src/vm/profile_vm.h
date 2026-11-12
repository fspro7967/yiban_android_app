#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// 档案 facade：老人与家属信息由配对/云端下发；看护人可在管理后台改紧急联系人（演示版只改内存）。
class ProfileVM : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString elderName READ elderName NOTIFY profileChanged)
    Q_PROPERTY(QString caregiverName READ caregiverName NOTIFY profileChanged)
    Q_PROPERTY(QString caregiverPhone READ caregiverPhone NOTIFY profileChanged)

public:
    explicit ProfileVM(QObject *parent = nullptr);

    QString elderName() const;
    QString caregiverName() const;
    QString caregiverPhone() const;

    // 配对流程成功后由 B2 调用，写入云端下发的结果。
    Q_INVOKABLE void applyProfile(const QString &elderName, const QString &caregiverName,
                                  const QString &caregiverPhone);

    // 管理后台：看护人手动改紧急联系人。演示版只改内存，真实版应走 /v1/profile。
    // 电话会先校验再按"138 0000 5678"/"020-1234 5678"的格式规范化保存。
    Q_INVOKABLE bool updateCaregiver(const QString &name, const QString &phone);

    // 电话号码校验：合法返回空串，否则返回给用户看的原因。
    // 支持中国大陆手机号（可带 +86 / 0086 前缀）和带区号的固定电话。
    Q_INVOKABLE QString phoneProblem(const QString &phone) const;
    // 规范化后的显示格式；不合法时原样返回。
    Q_INVOKABLE QString formatPhone(const QString &phone) const;

signals:
    void profileChanged();
};
