#!/usr/bin/env python3
"""假外设板：固件写出来之前，拿一台 Linux 电脑的蓝牙冒充外设板，联调 App 的 BLE 链路。

用 BlueZ 的 D-Bus 接口广播串口透传服务（NUS），消息格式与 src/comm/ble_link.h 的
BoardLinkProtocol、caremate_hardware/agent/硬件设计路线.md §2.3 一致：一行一条 JSON。

  收：主机写到 RX 的字节攒成行，逐行打印——能看到 App 每 5 秒一次的 home/host/heartbeat。
  发：主机订阅 TX 后，每 3 秒发一条 home/sensor/presence；故意按 20 字节一包拆开，
      考验主机一侧的拼行。

用法：python3 tools/fake_board.py [运行秒数，默认 120]
依赖：python3-dbus、python3-gi（Ubuntu 自带），蓝牙已打开；不需要 root。
然后在 App 的 管理后台 → 外设板 里扫描，会看到一台叫 YibanBoard 的设备。
"""假外设板：用本机蓝牙（BlueZ）广播串口透传服务，按硬件设计路线 §2.3 的格式收发。

收：主机写到 RX 的字节攒成行，逐行打印（能看到 5 秒一次的心跳）。
发：主机订阅 TX 后，每 3 秒发一条 home/sensor/presence；故意按 20 字节一包拆开发。
"""
import json, sys, time
import dbus, dbus.exceptions, dbus.mainloop.glib, dbus.service
from gi.repository import GLib

BLUEZ = 'org.bluez'
NUS = '6e400001-b5a3-f393-e0a9-e50e24dcca9e'
RX = '6e400002-b5a3-f393-e0a9-e50e24dcca9e'
TX = '6e400003-b5a3-f393-e0a9-e50e24dcca9e'
OM = 'org.freedesktop.DBus.ObjectManager'
PROPS = 'org.freedesktop.DBus.Properties'

def log(*a):
    print(time.strftime('%H:%M:%S'), *a, flush=True)

class App(dbus.service.Object):
    def __init__(self, bus):
        self.path = '/yiban/fake'
        dbus.service.Object.__init__(self, bus, self.path)
        self.service = Service(bus, self.path + '/service0')
    @dbus.service.method(OM, out_signature='a{oa{sa{sv}}}')
    def GetManagedObjects(self):
        out = {self.service.path: self.service.props()}
        for ch in self.service.chars:
            out[ch.path] = ch.props()
        return out

class Service(dbus.service.Object):
    def __init__(self, bus, path):
        self.path = dbus.ObjectPath(path)
        dbus.service.Object.__init__(self, bus, path)
        self.tx = Char(bus, path + '/char_tx', TX, ['notify'], self)
        self.rx = Char(bus, path + '/char_rx', RX, ['write', 'write-without-response'], self)
        self.chars = [self.tx, self.rx]
    def props(self):
        return {'org.bluez.GattService1': {
            'UUID': NUS, 'Primary': True,
            'Characteristics': dbus.Array([c.path for c in self.chars], signature='o')}}

class Char(dbus.service.Object):
    def __init__(self, bus, path, uuid, flags, service):
        self.path = dbus.ObjectPath(path); self.uuid = uuid; self.flags = flags
        self.service = service; self.notifying = False; self.rxbuf = b''; self.n = 0
        dbus.service.Object.__init__(self, bus, path)
    def props(self):
        return {'org.bluez.GattCharacteristic1': {
            'Service': self.service.path, 'UUID': self.uuid,
            'Flags': dbus.Array(self.flags, signature='s')}}
    @dbus.service.method(PROPS, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        return self.props()['org.bluez.GattCharacteristic1']
    @dbus.service.signal(PROPS, signature='sa{sv}as')
    def PropertiesChanged(self, iface, changed, invalidated):
        pass
    @dbus.service.method('org.bluez.GattCharacteristic1', in_signature='aya{sv}')
    def WriteValue(self, value, options):
        self.rxbuf += bytes(value)
        while b'\n' in self.rxbuf:
            line, self.rxbuf = self.rxbuf.split(b'\n', 1)
            log('RX <-', line.decode('utf-8', 'replace'), '(mtu=%s)' % options.get('mtu', '?'))
    @dbus.service.method('org.bluez.GattCharacteristic1')
    def StartNotify(self):
        if self.notifying: return
        self.notifying = True
        log('host subscribed to TX')
        GLib.timeout_add(3000, self.tick)
    @dbus.service.method('org.bluez.GattCharacteristic1')
    def StopNotify(self):
        self.notifying = False
        log('host unsubscribed')
    def tick(self):
        if not self.notifying: return False
        self.n += 1
        msg = {"topic": "home/sensor/presence", "payload": {
            "room": "living", "present": True, "moving": self.n % 2 == 0,
            "still_s": 60 * self.n, "dist_cm": 180, "ts": int(time.time() * 1000)}}
        data = (json.dumps(msg, separators=(',', ':')) + '\n').encode()
        log('TX ->', data.decode().strip())
        for i in range(0, len(data), 20):       # 按最小包长拆，考验主机的拼行
            self.PropertiesChanged('org.bluez.GattCharacteristic1',
                                   {'Value': dbus.Array(data[i:i+20], signature='y')}, [])
        return True

class Adv(dbus.service.Object):
    def __init__(self, bus):
        self.path = '/yiban/fake/adv0'
        dbus.service.Object.__init__(self, bus, self.path)
    @dbus.service.method(PROPS, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        return {'Type': 'peripheral', 'ServiceUUIDs': dbus.Array([NUS], signature='s'),
                'LocalName': dbus.String('YibanBoard'), 'Discoverable': dbus.Boolean(True)}
    @dbus.service.method('org.bluez.LEAdvertisement1')
    def Release(self):
        log('advertisement released')

def main():
    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    adapter = '/org/bluez/hci0'
    app = App(bus); adv = Adv(bus)
    loop = GLib.MainLoop()
    def fail(what):
        def f(e): log(what, 'FAILED:', e); loop.quit()
        return f
    dbus.Interface(bus.get_object(BLUEZ, adapter), 'org.bluez.GattManager1').RegisterApplication(
        app.path, {}, reply_handler=lambda: log('GATT app registered'), error_handler=fail('RegisterApplication'))
    dbus.Interface(bus.get_object(BLUEZ, adapter), 'org.bluez.LEAdvertisingManager1').RegisterAdvertisement(
        adv.path, {}, reply_handler=lambda: log('advertising as YibanBoard'), error_handler=fail('RegisterAdvertisement'))
    GLib.timeout_add_seconds(int(sys.argv[1]) if len(sys.argv) > 1 else 120, loop.quit)
    loop.run()

main()
