#!/usr/bin/env python3
"""假外设板：固件写出来之前，拿一台 Linux 电脑的蓝牙冒充外设板，联调 App 的 BLE 链路。

用 BlueZ 的 D-Bus 接口广播串口透传服务（NUS），消息格式与 src/comm/ble_link.h 的
BoardLinkProtocol、caremate_hardware/agent/硬件设计路线.md §2.3 一致：一行一条 JSON。

  收：主机写到 RX 的字节攒成行，逐行打印——能看到 App 每 5 秒一次的 home/host/heartbeat。
  发：主机订阅 TX 后，每 3 秒发一条 home/sensor/presence；故意按 20 字节一包拆开，
      考验主机一侧的拼行。

用法：
  python3 tools/fake_board.py                       # 默认叫 YibanBoard，跑 120 秒
  python3 tools/fake_board.py --name BoardB --seconds 300
  python3 tools/fake_board.py --ack 12000000881     # 订阅后 5 秒发一条板子确认
  python3 tools/fake_board.py --alarm               # 订阅后 5 秒发一条板子呼救
  python3 tools/fake_board.py --script 3:ack:12000000881,8:alarm   # 定时连发，测幂等

多块板：两台电脑各跑一个 --name 不同的实例；同一台机器上跑两个要加 --instance b
（D-Bus 对象路径不能重复）。这样能验收 B2-10"扫描列出两块、选中后只连一块"。

依赖：python3-dbus、python3-gi（Ubuntu 自带），蓝牙已打开；不需要 root。
"""
import argparse, json, time
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

CFG = {'name': 'YibanBoard', 'ack': None, 'alarm': False, 'script': []}

class App(dbus.service.Object):
    def __init__(self, bus, suffix):
        self.path = '/yiban/fake' + suffix
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
    def send_msg(self, msg):
        """一行 JSON + 按 20 字节分包 notify（Nordic UART 的最小 MTU）。"""
        data = (json.dumps(msg, separators=(',', ':')) + '\n').encode()
        log('TX ->', data.decode().strip())
        for i in range(0, len(data), 20):
            self.PropertiesChanged('org.bluez.GattCharacteristic1',
                                   {'Value': dbus.Array(data[i:i+20], signature='y')}, [])

    @dbus.service.method('org.bluez.GattCharacteristic1')
    def StartNotify(self):
        if self.notifying: return
        self.notifying = True
        log('host subscribed to TX')
        GLib.timeout_add(3000, self.tick)
        # B2-6：订阅后按参数定时发板子按键帧（--ack / --alarm / --script）。
        def fire(kind, occ):
            if kind == 'ack':
                self.send_msg({'topic': 'home/ack/' + CFG['name'], 'payload': {
                    'occurrence_id': int(occ), 'action': 'confirm', 'ts': int(time.time()*1000)}})
            elif kind == 'alarm':
                self.send_msg({'topic': 'home/alarm', 'payload': {
                    'level': 3, 'occurrence_id': 0, 'ts': int(time.time()*1000)}})
        if CFG['ack'] is not None:
            GLib.timeout_add_seconds(5, lambda: (fire('ack', CFG['ack']), False)[1])
        if CFG['alarm']:
            GLib.timeout_add_seconds(5, lambda: (fire('alarm', 0), False)[1])
        for delay, kind, occ in CFG['script']:
            GLib.timeout_add_seconds(delay, lambda k=kind, o=occ: (fire(k, o), False)[1])
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
        self.send_msg(msg)
        return True

class Adv(dbus.service.Object):
    def __init__(self, bus, suffix):
        self.path = '/yiban/fake' + suffix + '/adv0'
        dbus.service.Object.__init__(self, bus, self.path)
    @dbus.service.method(PROPS, in_signature='s', out_signature='a{sv}')
    def GetAll(self, iface):
        return {'Type': 'peripheral', 'ServiceUUIDs': dbus.Array([NUS], signature='s'),
                'LocalName': dbus.String(CFG['name']), 'Discoverable': dbus.Boolean(True)}
    @dbus.service.method('org.bluez.LEAdvertisement1')
    def Release(self):
        log('advertisement released')

def parse_script(text):
    """'3:ack:881,8:alarm' → [(3,'ack',881),(8,'alarm',0)]"""
    out = []
    for part in (text or '').split(','):
        bits = part.strip().split(':')
        if len(bits) < 2:
            continue
        try:
            delay = max(1, int(bits[0]))
            kind = bits[1]
            occ = int(bits[2]) if len(bits) > 2 else 0
        except ValueError:
            continue
        if kind in ('ack', 'alarm'):
            out.append((delay, kind, occ))
    return out

def main():
    global CFG
    parser = argparse.ArgumentParser(description='假外设板（BLE NUS，一行一条 JSON）')
    parser.add_argument('--name', default='YibanBoard', help='广播名；多板联调用不同名字区分')
    parser.add_argument('--instance', default='', help='D-Bus 路径后缀，如 b；同机跑多实例时用')
    parser.add_argument('--seconds', type=int, default=120, help='运行时长（秒），默认 120')
    parser.add_argument('--ack', type=int, help='订阅后 5 秒发一条 home/ack/<device>（ occurrence id）')
    parser.add_argument('--alarm', action='store_true', help='订阅后 5 秒发一条 home/alarm')
    parser.add_argument('--script', help='定时脚本：3:ack:881,8:alarm（秒:类型:occurrence id）')
    args = parser.parse_args()
    suffix = '/' + args.instance.strip('/') if args.instance else ''
    CFG = {'name': args.name, 'ack': args.ack, 'alarm': args.alarm,
           'script': parse_script(args.script)}

    dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
    bus = dbus.SystemBus()
    adapter = '/org/bluez/hci0'
    app = App(bus, suffix); adv = Adv(bus, suffix)
    loop = GLib.MainLoop()
    def fail(what):
        def f(e): log(what, 'FAILED:', e); loop.quit()
        return f
    dbus.Interface(bus.get_object(BLUEZ, adapter), 'org.bluez.GattManager1').RegisterApplication(
        app.path, {}, reply_handler=lambda: log('GATT app registered'), error_handler=fail('RegisterApplication'))
    dbus.Interface(bus.get_object(BLUEZ, adapter), 'org.bluez.LEAdvertisingManager1').RegisterAdvertisement(
        adv.path, {}, reply_handler=lambda: log('advertising as ' + CFG['name']), error_handler=fail('RegisterAdvertisement'))
    GLib.timeout_add_seconds(args.seconds, loop.quit)
    loop.run()

main()
