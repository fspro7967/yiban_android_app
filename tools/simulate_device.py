#!/usr/bin/env python3
"""模拟一台颐伴手机设备，完整走一遍「配对 → 拉计划/状态 → 上报事件 → 复查」。

默认打本地参考服务端（server/app.py），因此不需要云端令牌。
用户给出的设备标识写死在 DEVICE_ID，配对请求会带上它。

用法：
  python server/app.py --port 8400 --seed 888888
  python tools/simulate_device.py
  python tools/simulate_device.py --url http://<服务器IP>:8000/v1 --pair-code <6位码>
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

DEVICE_ID = "7zzbyPWqcCfUynCMuVu2hgji9Tb8lX4U"


def normalize(base: str) -> str:
    base = base.strip().rstrip("/")
    if not base.startswith(("http://", "https://")):
        raise ValueError("地址要以 http:// 或 https:// 开头")
    return base if base.endswith("/v1") else base + "/v1"


def call(method: str, url: str, token: str = "", body: dict | None = None):
    data = json.dumps(body, ensure_ascii=False).encode() if body is not None else None
    headers = {"Accept": "application/json"}
    if data:
        headers["Content-Type"] = "application/json; charset=utf-8"
    if token:
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    started = time.time()
    try:
        with urllib.request.urlopen(req, timeout=12) as resp:
            raw = resp.read().decode("utf-8", "replace")
            return resp.status, parse(raw), int((time.time() - started) * 1000)
    except urllib.error.HTTPError as exc:
        return exc.code, parse(exc.read().decode("utf-8", "replace")), int((time.time() - started) * 1000)


def parse(raw: str):
    try:
        return json.loads(raw)
    except Exception:
        return {}


def log(arrow: str, path: str, status: int, ms: int, detail: str = ""):
    print(f"  {arrow} {status:3d} {path:<22} {ms:4d}ms  {detail}")


def main() -> int:
    ap = argparse.ArgumentParser(description="模拟颐伴手机设备联调")
    ap.add_argument("--url", default="http://127.0.0.1:8400/v1")
    ap.add_argument("--pair-code", default="888888")
    ap.add_argument("--device-id", default=DEVICE_ID)
    args = ap.parse_args()
    try:
        base = normalize(args.url)
    except ValueError as exc:
        print("参数错误：" + str(exc))
        return 2

    print("模拟设备：" + args.device_id)
    print("服务端：" + base + "\n")

    print("1) 配对")
    status, body, ms = call("POST", base + "/pair", body={
        "pair_code": args.pair_code, "code": args.pair_code,
        "role": "elder", "device_id": args.device_id,
    })
    token = str(body.get("token") or "")
    log("→", "POST /pair", status, ms, "device_id=" + args.device_id[:12] + "…")
    if status not in (200, 201) or not token:
        print("  配对失败：HTTP %d %s" % (status, body))
        return 1
    print("  拿到令牌：" + token[:10] + "…\n")

    print("2) 建一条提醒并下拉状态")
    status, plan, ms = call("POST", base + "/plans", token, {
        "title": "模拟设备自测", "dose": "1片",
        "rule": {"kind": "daily", "times": ["23:59"]},
        "window_min": 15, "escalate_after_min": 2,
    })
    log("→", "POST /plans", status, ms, str(plan))
    if status not in (200, 201):
        print("  建计划失败")
        return 1

    status, state, ms = call("GET", base + "/state?since=-1", token)
    rows = state.get("occurrences") or []
    log("→", "GET /state", status, ms, "下发 %d 条实例" % len(rows))
    if status != 200 or not rows:
        print("  没有可确认的提醒实例，无法模拟上报")
        return 1
    occ = rows[0]
    occ_id = int(occ.get("occurrence_id") or occ.get("occurrenceId") or 0)
    old_state = str(occ.get("state") or "")
    action = "miss" if old_state == "pending" else "undo" if old_state == "confirmed" else "confirm"
    print("  实例 %d，当前状态 %s → 采用动作 %s\n" % (occ_id, old_state, action))

    print("3) 上报事件（模拟老人点了一下）")
    event_id = "sim-" + uuid.uuid4().hex[:16]
    status, ack, ms = call("POST", base + "/events", token, {"events": [{
        "event_id": event_id, "occurrence_id": occ_id, "action": action,
        "ts": int(time.time() * 1000), "device_id": args.device_id,
        "reason": "模拟设备自测，可忽略",
    }]})
    accepted = ack.get("accepted") or []
    rejected = ack.get("rejected") or []
    log("→", "POST /events", status, ms,
        "accepted=%d rejected=%d" % (len(accepted), len(rejected)))
    if status not in (200, 201) or not accepted or rejected:
        print("  上报被拒：%s" % ack)
        return 1

    print("4) 复查服务器状态")
    status, state2, ms = call("GET", base + "/state?since=-1", token)
    new = next((o for o in (state2.get("occurrences") or [])
                if int(o.get("occurrence_id") or o.get("occurrenceId") or 0) == occ_id), {})
    log("→", "GET /state", status, ms, "实例状态 %s → %s" % (old_state, new.get("state")))

    ok = str(new.get("state") or "") != old_state
    print("\n结论：" + ("模拟设备 → 服务器链路通，上行事件已被服务器端接收并改变状态。"
                     if ok else "事件被确认收到，但实例状态没变化，请检查服务端状态机。"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
