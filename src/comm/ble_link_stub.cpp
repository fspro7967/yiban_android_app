// 无 QtBluetooth 的桌面构建：BleLink 的空实现。
// 只为了让 BoardVM / QML 能编译、单测能跑；不会真的扫描或连接外设板。
// Android / 带 QtBluetooth 的桌面构建用 ble_link.cpp，本文件不参与编译（见 CMakeLists.txt）。

#include "ble_link.h"

BleLink::BleLink(QObject *parent)
    : DeviceLink(parent)
{
    m_state = QStringLiteral("unsupported");
    m_problem = QStringLiteral("当前构建未启用蓝牙");
}

bool BleLink::isUp() const { return false; }

bool BleLink::send(const QString &, const QJsonObject &) { return false; }

QVariantList BleLink::devices() const { return {}; }

QString BleLink::deviceName() const { return {}; }

bool BleLink::bound() const { return false; }

QString BleLink::detail() const { return m_problem; }

int BleLink::sentCount() const { return 0; }

int BleLink::receivedCount() const { return 0; }

void BleLink::startScan() {}

void BleLink::stopScan() {}

void BleLink::connectDevice(const QString &) {}

void BleLink::forget() {}

void BleLink::resume() {}
