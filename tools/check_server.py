#!/usr/bin/env python3
"""CareMate 端云联调自检（只用 Python 标准库）。

回答三个问题：
  1. 服务器活着吗？
  2. 手机端拿到的令牌有效吗？能拉到数据吗？
  3. 手机端上报的事件服务器收得到吗？（需要显式 --upload-test）

用法：
  python tools/check_server.py
  python tools/check_server.py --token ses-xxxxxxxx
  python tools/check_server.py --pair-code 123456
  python tools/check_server.py --token ses-xxxxxxxx --upload-test

默认地址就是手机端内置的云端地址。注意：--upload-test 会真的往服务端
写一条 confirm 事件（event_id 以 check- 开头），只在联调时用。
"""

import argparse
import json
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid

DEFAULT_URL = "http://101.245.109.50:8000/v1"
# 模拟设备的 device_id（用户提供的设备标识，写进配对请求）。
DEVICE_ID = "7zzbyPWqcCfUynCMuVu2hgji9Tb8lX4U"
TIMEOUT = 12


def normalize(base: str) -> str:
    base = base.strip().rstrip("/")
    if not base.startswith(("http://", "https://")):
        raise ValueError("服务器地址要以 http:// 或 https:// 开头")
    if not base.endswith("/v1"):
        base += "/v1"
    return base


def request(method: str, url: str, token: str = "", body: dict | None = None):
    data = None
    headers = {"Accept": "application/json"}
    if body is not None:
        data = json.dumps(body, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json; charset=utf-8"
    if token:
        # 与手机端 NetworkHttpClient 一致：Authorization: Bearer <token>
        headers["Authorization"] = "Bearer " + token
    req = urllib.request.Request(url, data=data, headers=headers, method=method)
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            raw = resp.read().decode("utf-8", "replace")
            return resp.status, parse_json(raw), raw
    except urllib.error.HTTPError as exc:
        raw = exc.read().decode("utf-8", "replace")
        return exc.code, parse_json(raw), raw
    except Exception as exc:  # DNS / 拒绝连接 / 超时
        return 0, {"error": str(exc)}, ""


def parse_json(raw: str):
    try:
        return json.loads(raw)
    except Exception:
        return {}


def report(ok: bool, name: str, detail: str):
    print(("  [OK]   " if ok else "  [FAIL] ") + name + "：" + detail)
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description="CareMate 端云联调自检")
    ap.add_argument("--url", default=DEFAULT_URL, help="服务端 /v1 地址")
    ap.add_argument("--token", default="", help="服务端签发的令牌（ses-…）")
    ap.add_argument("--pair-code", default="", help="6 位配对码，自检时用它换令牌")
    ap.add_argument("--device-id", default=DEVICE_ID, help="配对请求里的 device_id")
    ap.add_argument("--upload-test", action="store_true", help="额外上报一条测试事件")
    args = ap.parse_args()

    try:
        base = normalize(args.url)
    except ValueError as exc:
        print("参数错误：" + str(exc))
        return 2
    root = base[:-3].rstrip("/") or base
    token = args.token.strip()

    print("目标服务器：" + base)
    print("1) 服务器是否可达")
    status, _, raw = request("GET", root)
    # 只要服务端回了 HTTP 状态码（哪怕 401/404）就说明网络通；
    # 本地参考服务端把根路径也纳入鉴权，不能当成"连不上"。
    alive = report(status > 0, "服务端应答", f"HTTP {status}" if status else raw[:80])
    if not alive:
        print("\n结论：服务器连不上，先检查地址/端口/防火墙。")
        return 1

    if args.pair_code:
        print("\n2) 配对码换令牌")
        status, body, raw = request(
            "POST",
            base + "/pair",
            body={
                "pair_code": args.pair_code.strip(),
                "code": args.pair_code.strip(),
                "role": "elder",
                "device_id": args.device_id,
            },
        )
        got = str(body.get("token") or "")
        if report(status in (200, 201) and bool(got), "配对", f"HTTP {status}"):
            if not token:
                token = got
            print("       拿到令牌：" + got[:8] + "…（有效期 30 天）")
        else:
            print("       服务端返回：" + (raw[:200] or "空响应"))
            print("       提示：README 里的 8823 是初始值，云端可能已经换码，请找管理员要新的 6 位码。")

    print("\n3) 令牌与下行数据")
    if not token:
        report(False, "令牌", "未提供 --token，跳过（未 pairing 的手机端也是这个状态）")
        print("\n结论：服务器可达，但还没有令牌，无法确认手机端能否收到数据。")
        return 1

    status, body, raw = request("GET", base + "/state?since=0", token=token)
    if status == 200:
        occurrences = body.get("occurrences") or []
        # 本地参考服务端首建实例的 updated_seq=0，since=0 会把它过滤掉；
        # 自检需要一条实例来构造测试事件，回退到 -1 再拿一次。
        if args.upload_test and not occurrences:
            status2, body2, raw2 = request("GET", base + "/state?since=-1", token=token)
            if status2 == 200 and (body2.get("occurrences") or []):
                status, body, raw = status2, body2, raw2
                occurrences = body["occurrences"]
        report(True, "令牌有效", f"HTTP 200，下发 {len(occurrences)} 条提醒状态，游标 {body.get('next_seq')}")
    elif status == 401:
        report(False, "令牌有效", "HTTP 401：令牌无效或已过期，需要重新配对/换令牌")
        print("\n结论：服务器可达，但手机端当前令牌收不到任何数据。")
        return 1
    else:
        report(False, "令牌有效", f"HTTP {status}：{(raw or '')[:160]}")

    if not args.upload_test:
        print("\n结论：服务器通信正常、令牌可以拉数据；事件上传未测（加 --upload-test 开启）。")
        return 0

    print("\n4) 事件上行（会写入一条测试数据）")
    rows = (body.get("occurrences") or []) if isinstance(body, dict) else []
    now_ms = int(time.time() * 1000)
    # 服务端规则：pending 只能 confirm/miss/snooze，confirmed 的 undo 只有 5 秒窗口。
    # 所以优先挑一条"未来的 pending"实例，不碰临近/已结束的真实提醒。
    pending = [r for r in rows if isinstance(r, dict)
               and str(r.get("state")) == "pending"
               and (r.get("occurrence_id") or r.get("occurrenceId"))]
    future = [r for r in pending if int(r.get("due_ts") or 0) > now_ms + 3600_000]
    target = (future or pending or [None])[0]
    if target is None:
        report(False, "测试事件", "服务端没有可确认的 pending 提醒实例，无法构造合法事件")
        print("       可以先在服务端建一条计划/提醒，或用 --url 指向本地参考服务端再测。")
        return 1

    occurrence_id = target.get("occurrence_id") or target.get("occurrenceId")
    event_id = "check-" + uuid.uuid4().hex[:16]
    # 对 pending 上报 confirm（老人按"我吃了"的正常动作），服务端会写入一条测试事件。
    action = "confirm"
    payload = {
        "events": [
            {
                "event_id": event_id,
                "occurrence_id": int(occurrence_id),
                "action": action,
                "ts": int(time.time() * 1000),
                "reason": "联调自检，可忽略",
            }
        ]
    }
    status, ack, raw = request("POST", base + "/events", token=token, body=payload)
    accepted = ack.get("accepted") or []
    rejected = ack.get("rejected") or []
    if status in (200, 201) and accepted and not rejected:
        report(True, "事件上传", f"HTTP {status}，服务器已确认 event_id={event_id}（动作 {action}）")
        print("\n结论：手机端 → 服务器这条链路是通的（上行、下行都验证过）。")
        return 0

    report(False, "事件上传", f"HTTP {status}：{(raw or '')[:240]}")
    print("\n结论：下行正常，但事件被服务器拒绝，检查 action / occurrence_id / 状态冲突。")
    return 1


if __name__ == "__main__":
    sys.exit(main())
