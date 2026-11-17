# 外设板蓝牙链路 真机测试记录

> 测试日期：2026-11-17
> 被测版本：main `7dcdd9b`（`feat_B2_platform` 合入 main 后的版本），Debug 构建
> 测试机：Redmi Note 11T Pro（型号 22041216C，HyperOS 2 / Android 14，已 root）
> 外设板：用电脑蓝牙模拟（`tools/fake_board.py`），**不是真固件**

## 结论

App 在前台时，蓝牙链路从扫描到解除绑定共 10 项全部通过。

**有 1 项不通过：手机灭屏或 App 切到后台后，主机心跳会停**，回到前台才恢复。
接上真外设板后，灭屏 15 秒板子就会判定主机掉线。详见[第五节](#五发现的问题)。

---

## 一、测试总体思路

### 1. 测什么

外设板链路在 `src/comm/ble_link.*`，底层收发在 `src/devicehandler.cpp`，管理后台的「外设板」卡片（`qml/BoardCard.qml`）是唯一入口。
按"一块板从第一次用到不再用"的完整生命周期来测：

```text
权限 / 蓝牙开关 → 扫描 → 连接 → 双向收发 → 断线重连 → 重启 App 连回 → 长时间离线后恢复 → 解除绑定
```

每个环节都要看两种情况：一是正常路径，二是会出错的路径，比如蓝牙没开、没给权限、板子掉电、板子不在。

### 2. 用模拟板代替真板

外设板固件还没写，但链路协议已经定死（`src/comm/ble_link.h` 的 `BoardLinkProtocol`）：

- 走 Nordic UART Service（NUS），RX 特征写入，TX 特征通知；
- 一行一条 JSON；
- 主机每 5 秒发一次心跳。

所以用电脑的蓝牙按同一协议冒充外设板：`tools/fake_board.py` 通过 BlueZ 广播一台叫 `YibanBoard` 的设备，

- 收：把主机写进 RX 的字节拼成行打印出来，能看到每一条心跳；
- 发：每 3 秒发一条 `home/sensor/presence`，**故意按 20 字节一包拆开**，考验 App 一侧的拼行逻辑。

这样测出来的是**手机一侧**的行为。真板子的射频、功耗、固件解析都不在本次范围内。

### 3. 怎么判断通过

只看界面不够，因为"已连接"可能是假象（见 4.3 的教训）。每一项都同时核对三处：

| 证据 | 来源 | 能证明什么 |
|---|---|---|
| App 状态日志 | `adb logcat --pid=<App 进程>`，tag `yiban.board` | 状态机走到哪一步（idle / connecting / discovering / ready）、重连退避 |
| 模拟板日志 | `fake_board.py` 的输出 | 对端**真的**收到了心跳、主机**真的**订阅了通知 |
| 链路状态 | 电脑上 `bluetoothctl devices Connected` | 底层 BLE 连接是否还在 |
| 界面 | 手机截图或测试人员口述 | 用户看到的提示是否正确 |

### 4. 人机分工

HyperOS 不允许 adb 模拟点击（`input tap`），也不允许 adb 收回权限（`pm revoke`），两者都需要"USB 调试（安全设置）"。
所以本次分成两部分：

- **测试人员在手机上点**：输入管理后台验证语、扫描、连接、解除绑定、改权限；
- **电脑侧负责制造故障并核对日志**：启停模拟板、断开 BLE 链路、开关手机蓝牙、强制重启 App。

---

## 二、测试环境与准备

| 项目 | 内容 |
|---|---|
| 电脑 | Linux，BlueZ，蓝牙控制器 `18:CC:18:FC:A7:E1`，有 `python3-dbus`、`python3-gi` |
| 手机蓝牙地址 | `F8:AB:82:98:DE:E4` |
| APK | 合并后的 main 在本机构建的 Debug 包，`adb install -r` 覆盖安装 |
| 权限初始状态 | `BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT` 之前已授予 |

> 读日志时注意：**手机时钟比电脑快约 1 小时 49 分**。App 日志用手机时间，模拟板日志用电脑时间。
> 例如 App 的 23:50:36 对应电脑的 22:01:43。

准备步骤：

```bash
# 1. 安装
adb install -r build/android-build/yiban_android_app.apk
adb shell svc bluetooth enable

# 2. 启动模拟板（参数是运行秒数）
python3 -u tools/fake_board.py 900 > fake_board.log 2>&1 &
#   看到下面两行说明在广播了：
#   GATT app registered
#   advertising as YibanBoard

# 3. 启动 App，按进程抓全部日志
#   注意：按 tag 过滤会漏掉蓝牙模块，Qt 用日志分类名 yiban.board 当 tag
adb shell monkey -p com.yiban.elder -c android.intent.category.LAUNCHER 1
adb logcat -v time --pid=$(adb shell pidof com.yiban.elder) > app.log &
```

启动日志里应有 `bound board: "none"`，表示从没绑定过板子的干净状态。

进入外设板页面：首页 → 设置 → 往下滑 → 进入管理后台 → 输入"我是看护人" → 「外设板」卡片。

---

## 三、测试步骤与结果

### 3.1 扫描与连接 ✅

**操作**：点「扫描附近的外设板」→ 列表出现 `YibanBoard` → 点它。

**App 日志**：

```text
23:38:55.945 state: connecting YibanBoard
23:39:03.791 state: discovering YibanBoard
23:39:05.936 state: ready YibanBoard
```

**结果**：只列出带串口透传服务的设备，从点击到 ready 约 10 秒。

### 3.2 双向收发 ✅

**模拟板日志**：

```text
21:50:15 host subscribed to TX
21:50:15 RX <- {"payload":{"ts":1794929945963},"topic":"home/host/heartbeat"} (mtu=517)
21:50:18 TX -> {"topic":"home/sensor/presence","payload":{"room":"living","present":true,...}}
21:50:20 RX <- {"payload":{"ts":1794929951218},"topic":"home/host/heartbeat"} (mtu=517)
```

**App 日志**：每 3 秒一条 `rx home/sensor/presence`，没有出现格式错误的行（malformed）。

**界面**：

- 已连接 YibanBoard
- 已发出 4 条（含每 5 秒一次的心跳），收到 5 条
- 雷达：有人，静止 5 分钟，距离约 1.8 米
- 最近收到：完整的 presence JSON

**结果**：

| 检查点 | 结果 |
|---|---|
| 心跳间隔 | 5 秒 ✅ |
| MTU | 协商到 517 ✅（一条心跳 63 字节，一个包就能发完） |
| 20 字节拆包后能否拼回 | ✅ |
| 收发计数与两端日志一致 | ✅ |

### 3.3 断线自动重连 ✅

**模拟方法**：在电脑上主动断开链路，相当于板子断电。

```bash
bluetoothctl disconnect F8:AB:82:98:DE:E4
```

**App 日志**：

```text
23:50:36.686 state: idle
23:50:36.750 next reconnect in 2000 ms
23:50:38.889 reconnecting to "YibanBoard" "18:CC:18:FC:A7:E1"
23:50:43.121 state: discovering YibanBoard
23:50:45.287 state: ready YibanBoard
```

**结果**：断线立刻检测到，2 秒后重连，约 9 秒恢复。模拟板重新收到订阅和心跳。

### 3.4 重启 App 后自动连回 ✅

**操作**（电脑侧）：

```bash
adb shell am force-stop com.yiban.elder
adb shell monkey -p com.yiban.elder -c android.intent.category.LAUNCHER 1
```

**App 日志**：

```text
23:50:56.074 bound board: "YibanBoard 18:CC:18:FC:A7:E1"
23:50:57.997 reconnecting to "YibanBoard" "18:CC:18:FC:A7:E1"
23:51:04.231 state: ready YibanBoard
```

**结果**：读到绑定记录后自动连回，**没有弹授权框**，约 8 秒就绪。

### 3.5 长时间离线后恢复 ✅

**操作**：停掉模拟板，并断开链路，让板子"不在了"。

```bash
kill <fake_board.py 的 pid>
bluetoothctl disconnect F8:AB:82:98:DE:E4
```

**App 日志**（板子不在期间）：

```text
23:52:09.710 next reconnect in 2000 ms
23:52:11.853 state: connecting YibanBoard
23:52:42.041 connect to "YibanBoard" failed: "Error occurred trying to connect to remote device."
23:52:42.042 next reconnect in 4000 ms
23:52:45.922 state: connecting YibanBoard
```

约 1 分钟后重新启动模拟板（电脑 22:04:15），App 在挂着的这次连接请求里直接连上：

```text
23:53:07.454 state: discovering YibanBoard
23:53:09.826 state: ready YibanBoard
```

**结果**：

- 重连间隔按退避拉长：2 秒 → 4 秒；
- 每次连接尝试约 30 秒超时，这是 Android 系统 GATT 的默认超时；
- 板子回来后 4 秒内恢复。

### 3.6 解除绑定 ✅

**操作**：点「解除绑定」。

**结果**：

- App 日志只剩一行 `state: idle`，后面**没有** `next reconnect`；
- 模拟板打出 `host unsubscribed`，心跳停止；
- `bluetoothctl devices Connected` 为空；
- 再等 20 秒，仍然没有任何重连。

### 3.7 手机蓝牙关闭时扫描 ✅

**操作**：电脑侧执行 `adb shell svc bluetooth disable`，测试人员点扫描。

**结果**：提示「请先打开手机蓝牙」（错误码 `bluetooth_off`）。

### 3.8 拒绝与恢复蓝牙权限 ✅

**操作**（全部在手机上）：

1. 系统设置 → 应用 → 颐伴 → 权限 → 附近的设备 → 不允许；
2. 重新打开 App，扫描，在系统授权框里点「不允许」；
3. 回系统设置改回「允许」，再扫描。

**结果**：

- 拒绝时提示「没有蓝牙权限，请到系统设置里允许"附近的设备"」（错误码 `bluetooth_denied`）；
- 改回允许后能扫到 `YibanBoard`；
- 最后核对：`BLUETOOTH_SCAN`、`BLUETOOTH_CONNECT` 都是 `granted=true`。

### 3.9 灭屏与后台 ❌

测试中途手机自动灭屏（手机时间 23:41:15），App 日志：

```text
23:41:11.385 DeviceHandler::writeData 63 bytes -> 1 chunks    ← 最后一条心跳
23:41:15.937 makeCurrent(): no EGLSurface, likely Surface destroyed by Android.
（此后没有任何日志，直到解锁）
23:49:56.487 DeviceHandler::writeData 63 bytes -> 1 chunks    ← 解锁回到前台后才恢复
```

同期检查：

- App 进程还活着（同一个 pid），处于普通后台状态，**没有被系统冻结**，也没有前台服务；
- BLE 链路还连着（`bluetoothctl` 显示已连接）。

**结果**：心跳停了将近 9 分钟。原因见第五节。

---

## 四、测试中踩到的坑（复测时注意）

1. **`tools/fake_board.py` 原本跑不起来。**
   BLE 分支提交 `37d266a` 时，文件开头的文档字符串少了结尾的 `"""`，一运行就报 `SyntaxError: invalid character '：'`。
   本次已修，只补引号、删掉一段重复说明，逻辑没动。
2. **按 tag 过滤 logcat 会漏掉蓝牙日志。**
   Qt 用日志分类名当 tag，蓝牙模块的 tag 是 `yiban.board`。直接用 `--pid` 按进程抓最省事。
3. **只停模拟板脚本，模拟不出断线。**
   停脚本只是注销了 GATT 服务和广播，手机和电脑的 BLE 连接还在，App 会一直以为自己连着。
   之后重开脚本，App 的写入走的仍是旧连接，新服务一条也收不到，看起来像"僵尸连接"。
   这是模拟方式造成的假象，真板子断电时链路一定会断。**模拟断线要用 `bluetoothctl disconnect <手机地址>`。**
4. **不要用 `pkill -f fake_board.py`。**
   它会匹配到执行这条命令的 shell 本身，把命令一起杀掉，后面的步骤就不执行了。改成先查 pid 再 `kill`。
5. **HyperOS 限制 adb。** `input tap/swipe` 和 `pm revoke` 都会报 `SecurityException`。
   要么在开发者选项里开「USB 调试（安全设置）」，要么由测试人员手动操作。
   `svc bluetooth enable/disable`、`am force-stop`、`monkey` 不受影响。
6. **测试时别让手机灭屏。** 否则会触发 3.9 的问题，干扰其它项的判断。
   可以临时把自动锁屏调长，或者插着 USB 时在开发者选项里开「保持唤醒状态」。
7. **模拟板的小毛病**：断线时它不会重置"已订阅"标志，所以重连后不会再打印 `host subscribed`，但收发都正常。只影响日志，不影响判断。

---

## 五、发现的问题

### P1 灭屏或切到后台后主机心跳停止

- **现象**：见 3.9，App 进后台后不再发心跳，也不处理收到的数据，回到前台才恢复。
- **影响**：接上真外设板后，灭屏超过 15 秒，板子就会按掉线降级：按缓存继续亮灯，角上亮灰点。
  老人机大部分时间是灭屏的，所以这个功能在真实使用中基本处于降级状态。
- **原因**：`android/AndroidManifest.xml` 没有设置 `android.app.background_running`。
  这个开关默认是 false，App 进后台时 Qt 会暂停事件循环，心跳的 `QTimer` 跟着停。
- **建议修法**：
  1. manifest 里把 `android.app.background_running` 设为 `true`；
  2. 和外设板保持连接期间挂一个前台服务（可以参考 `AlarmService` 的写法），否则 HyperOS 照样会限制后台进程；
  3. 修完复测：灭屏 5 分钟，模拟板日志里心跳不能断。
- **来源**：不是这次合并引入的。`caremate_hardware/agent/硬件设计路线.md` 原本就把"接进前台服务"排在"前台跑通"之后。

### 顺带发现（与蓝牙无关）

启动诊断显示 `全屏提醒=false`，说明 root 下用 `cmd notification allow_fsi` 自动放行全屏提醒，在这台机器上没生效。
建议并入 `agent/功能优化清单.md` 的 O-2（首次安装检查权限）。

---

## 六、本次未覆盖

- **真外设板**：射频距离、穿墙、ESP32-S3 固件的解析和降级显示，等固件写好后再测；
- **板子主动上报的其它 topic**：`home/ack`（确认键）、`home/alarm`。模拟板只发 presence；
- **主机下发提醒**：`home/reminder/<device>` 发给灯板的链路，当前主机侧还没接；
- **扫描时周围有多块板**：只测了一块；
- **"拒绝后再次扫描是否还弹框"**：测试人员没有单独反馈这一步，界面上拒绝和恢复两条路径都已确认正常。
