#!/usr/bin/env python3
"""稳定性 chaos 自检：断网补传 / 重启不丢 / 改时间 / 连点（1124TODO B2-3）。

四项可单独跑，也可一次跑完，每项输出 PASS / FAIL / MANUAL 和关键数字：

    python tools/chaos.py --all
    python tools/chaos.py --case network
    python tools/chaos.py --case restart
    python tools/chaos.py --case clock
    python tools/chaos.py --case taps

设计说明：
  * 只用 adb（自动在常见 SDK 路径/ADB 环境变量里找），不装第三方 Python 包。
  * HyperOS 上 `input tap` 会被系统拦（无障碍/防误触），所以"连点"不模拟屏幕
    点击，改成通过 `am broadcast` 触发本机调试入口；没有调试入口时降级为
    MANUAL，打印精确的操作步骤，不假装 PASS。
  * 改系统时间需要 su 或人手，`--case clock` 只做前后读数 + 提示，不会去改
    系统时间（改了很难恢复，且 AI 环境通常被权限规则拒绝）。
  * 待上传条数从 adb logcat 抓 "待上传 N 条" / "syncState" 日志；抓不到时
    输出 MANUAL 提示从管理后台右上角读数字，由人填进 --pending-before /
    --pending-after 让脚本判定。绝不把"读不到"当 0 条。

用法示例：
    python tools/chaos.py --all --package com.yiban.elder
    python tools/chaos.py --case network --pending-after 0
    python tools/chaos.py --case taps --debug-action com.yiban.elder.DEBUG_CONFIRM
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import time

PKG_DEFAULT = "com.yiban.elder"
ACTIVITY_DEFAULT = "com.yiban.elder/.MainActivity"

# 从 logcat 里读管理页/manual sync 写下的 "待上传 N 条" 数字。
PENDING_RE = re.compile(r"待上传\s*(\d+)\s*条|pending(?:Count)?\D{0,6}(\d+)")


def find_adb(explicit=None):
    if explicit:
        return explicit
    if os.environ.get("ADB"):
        return os.environ["ADB"]
    found = shutil.which("adb")
    if found:
        return found
    for path in (
        "D:/yiban-toolchain/android-sdk/platform-tools/adb.exe",
        os.path.expanduser("~/AppData/Local/Android/Sdk/platform-tools/adb.exe"),
        "/usr/local/bin/adb",
    ):
        if os.path.isfile(path):
            return path
    return None


class Adb:
    def __init__(self, binary, serial=None):
        self.binary = binary
        self.serial = serial

    def run(self, args, timeout=30):
        cmd = [self.binary]
        if self.serial:
            cmd += ["-s", self.serial]
        cmd += args
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace", timeout=timeout)
            return proc.returncode, (proc.stdout or "") + (proc.stderr or "")
        except subprocess.TimeoutExpired:
            return -1, "TIMEOUT"
        except FileNotFoundError:
            return -1, "adb 不存在"

    def shell(self, command, timeout=30):
        return self.run(["shell", command], timeout)

    def available(self):
        code, out = self.run(["devices"])
        return code == 0 and bool(re.search(r"\bdevice\b", out))


def read_pending(adb, package, lines=400):
    """从 logcat 缓存里取最后一次出现的待上传数字；读不到返回 None。"""
    code, out = adb.shell(f"logcat -d -t {lines}")
    if code != 0:
        return None
    number = None
    for line in out.splitlines():
        m = PENDING_RE.search(line)
        if m:
            number = int(m.group(1) or m.group(2))
    return number


MANUAL_CASES = set()

def write_result(name, status, detail):
    if status == "MANUAL":
        MANUAL_CASES.add(name)
    print(f"[{status:6s}] {name}: {detail}")


def check_network(adb, args):
    """断网 → 制造积压 → 恢复 → 60 秒内 pending 归零。"""
    if not args.yes:
        print("说明：断网期间请在设备上点出几条确认/记录，脚本会读 pending 并等恢复。")
    adb.shell("svc wifi disable")
    adb.shell("svc data disable")
    write_result("断网", "INFO", "wifi/data 已关闭")
    print("  >>> 现在在手机上点出几条记录/确认（或让同伴操作），完成后回车继续...")
    if args.non_interactive:
        time.sleep(args.hold_sec)
    else:
        try:
            input()
        except EOFError:
            time.sleep(args.hold_sec)

    before = args.pending_before if args.pending_before is not None else read_pending(adb, args.package)
    if before is None:
        write_result("断网补传", "MANUAL",
                     "读不到 pending：请从管理后台右上角读数字，用 --pending-before N 重跑")
        adb.shell("svc wifi enable")
        adb.shell("svc data enable")
        return False

    adb.shell("svc wifi enable")
    adb.shell("svc data enable")
    write_result("恢复网络", "INFO", "wifi/data 已开启，最多等 60 秒")
    deadline = time.time() + 60
    after = None
    while time.time() < deadline:
        time.sleep(3)
        after = args.pending_after if args.pending_after is not None else read_pending(adb, args.package)
        if after == 0:
            break
    if after is None:
        write_result("断网补传", "MANUAL",
                     f"断网时 pending={before}，恢复后读不到；用 --pending-after N 重跑")
        return False
    ok = after == 0
    write_result("断网补传", "PASS" if ok else "FAIL",
                 f"pending {before} → {after}（60 秒内 {'归零' if ok else '未归零'}）")
    return ok


def check_restart(adb, args):
    """有积压时连续 force-stop + 启动，pending 不减、状态不变。"""
    counts = []
    for i in range(1, args.restart_times + 1):
        adb.shell(f"am force-stop {args.package}")
        time.sleep(1)
        adb.shell(f"monkey -p {args.package} -c android.intent.category.LAUNCHER 1")
        time.sleep(args.launch_wait)
        n = args.pending_before if args.pending_before is not None else read_pending(adb, args.package)
        counts.append(n)
        write_result(f"重启 {i}/{args.restart_times}", "INFO", f"pending={n}")
    known = [c for c in counts if c is not None]
    if len(known) != len(counts):
        write_result("重启不丢", "MANUAL",
                     "部分轮次读不到 pending；用 --pending-before N 重跑并人工核对提醒状态")
        return False
    ok = len(set(known)) == 1
    write_result("重启不丢", "PASS" if ok else "FAIL",
                 f"pending 序列 {known}（{'恒定' if ok else '有变化'}）")
    return ok


def check_clock(adb, args):
    """改时间：脚本只读当前时间并打印人工步骤，不擅自改系统时间。"""
    code, out = adb.shell("date")
    now = out.strip() if code == 0 else "<读不到>"
    write_result("改时间", "MANUAL", f"当前设备时间：{now}")
    print("  步骤 1（跨天）：开发者选项/系统设置把日期调到明天 → 检查今天的提醒归档、明天按时生成。")
    print("  步骤 2（回拨）：把时间往回拨 1 小时 → 已确认的不变待确认、不重复响。")
    print("  需要 su 时用：adb shell 'date -s 20261128.080000'（本脚本不会自动执行）。")
    return False  # 人工项不算 FAIL，也不算 PASS


def check_taps(adb, args):
    """连点：走调试广播，不走 input tap（HyperOS 上会被拦）。"""
    if not args.debug_action:
        write_result("连点", "MANUAL",
                     "没有调试广播；请用调试入口连续调 ReminderVM.confirm 10 次，"
                     "确认只产生 1 条 confirm 事件。传 --debug-action <action> 可自动跑。")
        return False
    for _ in range(10):
        adb.shell(f"am broadcast -a {args.debug_action} -n {args.package}/.DebugReceiver "
                  f"--es action confirm --el occurrence_id {args.occurrence_id}")
        time.sleep(0.2)
    time.sleep(3)
    code, out = adb.shell(f"logcat -d -t 300 | grep -c 'event_id.*confirm'")
    count = int(out.strip()) if code == 0 and out.strip().isdigit() else 0
    ok = count == 1
    write_result("连点", "PASS" if ok else "FAIL",
                 f"10 次调用产生 confirm 事件 {count} 条（期望 1）")
    return ok


CASES = {
    "network": ("断网补传", check_network),
    "restart": ("重启不丢", check_restart),
    "clock": ("改时间", check_clock),
    "taps": ("连点", check_taps),
}


def main(argv=None):
    parser = argparse.ArgumentParser(description="CareMate 稳定性 chaos 自检（1124TODO B2-3）")
    parser.add_argument("--case", choices=list(CASES), help="只跑一项")
    parser.add_argument("--all", action="store_true", help="跑全部四项")
    parser.add_argument("--adb", help="adb 可执行文件路径")
    parser.add_argument("--serial", help="设备序列号（多设备时指定）")
    parser.add_argument("--package", default=PKG_DEFAULT, help=f"应用包名（默认 {PKG_DEFAULT}）")
    parser.add_argument("--pending-before", type=int, help="人工填入操作前的待上传条数")
    parser.add_argument("--pending-after", type=int, help="人工填入恢复后的待上传条数")
    parser.add_argument("--restart-times", type=int, default=10, help="重启轮数（默认 10）")
    parser.add_argument("--launch-wait", type=int, default=5, help="启动后等待秒数（默认 5）")
    parser.add_argument("--hold-sec", type=int, default=30, help="非交互模式下断网等待秒数")
    parser.add_argument("--non-interactive", action="store_true", help="不等待回车，按 --hold-sec 计时")
    parser.add_argument("--yes", action="store_true", help="跳过前置说明")
    parser.add_argument("--debug-action", help="连点用的调试广播 action（如 com.yiban.elder.DEBUG_CONFIRM）")
    parser.add_argument("--occurrence-id", type=int, default=0, help="连点针对的 occurrence id")
    args = parser.parse_args(argv)

    if not args.case and not args.all:
        parser.error("请指定 --case <name> 或 --all")

    adb_bin = find_adb(args.adb)
    if not adb_bin:
        print("找不到 adb：请设 ADB 环境变量或传 --adb <path>")
        return 2
    adb = Adb(adb_bin, args.serial)
    if not adb.available():
        print(f"adb 可用但没有已连接的设备：{adb_bin}")
        return 2

    selected = list(CASES) if args.all else [args.case]
    results = {}
    for name in selected:
        label, func = CASES[name]
        print(f"\n=== {label} ===")
        try:
            results[label] = func(adb, args)
        except Exception as exc:  # 单项失败不影响其余项
            write_result(label, "FAIL", f"脚本异常：{exc}")
            results[label] = False

    print("\n=== 汇总 ===")
    for label, ok in results.items():
        print(f"  {label}: {'PASS' if ok is True else ('MANUAL' if label in MANUAL_CASES else 'FAIL')}")
    # 人工项不算脚本失败；只有明确的 FAIL 才让退出码非 0。
    return 1 if any(v is False and label not in MANUAL_CASES
                    for label, v in results.items()) else 0


if __name__ == "__main__":
    sys.exit(main())
