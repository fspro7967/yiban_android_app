#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

// 档案 facade（只读）：老人与家属信息由配对/云端下发，端上不发起修改。
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

signals:
    void profileChanged();
};
