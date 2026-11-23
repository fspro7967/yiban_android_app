#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""颐伴联调服务端（零依赖：Python 3.8+ 标准库 + sqlite3）。

按 docs/端上接口规范_v1.1.md 第 8 节实现：

    POST   /v1/pair            配对：配对码换 token + 档案
    POST   /v1/events          上传事件（按 event_id 幂等去重）
    GET    /v1/state?since=N   拉取 occurrence 状态（增量游标 next_seq）
    GET    /v1/plans?since=N   拉取计划（增量）
    POST   /v1/alarm           呼救/升级告警
    POST   /v1/plans           新建计划
    PATCH  /v1/plans/{id}      修改计划（version+1）
    DELETE /v1/plans/{id}      软删除计划
    POST   /v1/records         新建记录
    PATCH  /v1/records/{id}    修订记录

设计要点（与规范对齐）：
  * server_seq 单调递增，所有写入（计划、occurrence 状态、记录）都盖 updated_seq；
    端上用 ?since= 增量拉，持久化已应用到的 seq。
  * /v1/events 按 event_id 去重：重试重放不会重复记账，重复提交返回 applied=false。
  * occurrence_id 用端上冻结的公式：plan_id*1e9 + (julian%1e7)*100 + 当日序号，
    端上/服务端对同一条 occurrence 永远是同一个数。
  * 每天 0 点后首次访问时按计划 rule（daily/weekly/monthly/once）生成当天的
    occurrence（pending），并把过窗口的自动转 missed、推迟到点的转回 notified。
  * 冲突以服务端为准（规范 §9）：端上 confirm 之后服务端状态是唯一真相。

联调：
    python server/app.py --port 8000
    配对码任意 6 位数字（首次使用即建档），老人端在管理页把服务器地址改成
    http://<本机局域网IP>:8000/v1 即可。

真实部署时把配对码改为预登记制（INSERT households），并加 HTTPS。
"""

import argparse
import json
import re
import sqlite3
import threading
import uuid
from datetime import date, datetime, timedelta
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# ---------------------------------------------------------------- 常量

SNOOZE_MS = 10 * 60 * 1000            # 推迟一步 = 10 分钟（规范 §4.1）
MAX_SNOOZE = 3
JULIAN_EPOCH = date(1858, 11, 17)     # 与端上 QDate::toJulianDay 一致的儒略日
EVENT_NAMESPACE = uuid.UUID("7b0f7d3e-3c2a-4a55-9f0e-6b1a2c8d4e11")

VALID_ACTIONS = {"fire", "confirm", "snooze", "undo", "miss", "escalate", "pair", "sos"}


def now_ms() -> int:
    return int(datetime.now().timestamp() * 1000)


def today_str() -> str:
    return date.today().isoformat()


def occurrence_id_for(plan_id: int, day: date, index: int) -> int:
    julian = day.toordinal() + JULIAN_EPOCH.toordinal() + 1  # 近似儒略日，只需与端上同源
    # 端上公式：planId*1e9 + (julianDay % 1e7)*100 + index
    return plan_id * 1_000_000_000 + (julian % 10_000_000) * 100 + index


# ---------------------------------------------------------------- 数据库

class Store:
    """单文件 SQLite。线程锁包住所有事务（联调规模足够，不需要 WAL 玄学）。"""

    def __init__(self, path: str):
        self.db = sqlite3.connect(path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.lock = threading.RLock()
        self._migrate()

    def _migrate(self):
        with self.lock, self.db:
            self.db.executescript(
                """
                CREATE TABLE IF NOT EXISTS households(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    pair_code TEXT UNIQUE NOT NULL,
                    elder_name TEXT DEFAULT '',
                    caregiver_name TEXT DEFAULT '',
                    caregiver_phone TEXT DEFAULT '',
                    seq INTEGER NOT NULL DEFAULT 0,
                    next_plan_id INTEGER NOT NULL DEFAULT 1,
                    next_record_id INTEGER NOT NULL DEFAULT 1,
                    day TEXT DEFAULT ''
                );
                CREATE TABLE IF NOT EXISTS devices(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    household_id INTEGER NOT NULL,
                    device_id TEXT NOT NULL,
                    role TEXT DEFAULT 'elder',
                    token TEXT NOT NULL,
                    UNIQUE(household_id, device_id)
                );
                CREATE TABLE IF NOT EXISTS plans(
                    household_id INTEGER NOT NULL,
                    plan_id INTEGER NOT NULL,
                    title TEXT NOT NULL,
                    dose TEXT DEFAULT '',
                    note TEXT DEFAULT '',
                    rule_json TEXT NOT NULL,
                    window_min INTEGER DEFAULT 30,
                    escalate_after_min INTEGER DEFAULT 15,
                    allow_late INTEGER DEFAULT 1,
                    skip_dates TEXT DEFAULT '[]',
                    version INTEGER DEFAULT 1,
                    deleted INTEGER DEFAULT 0,
                    updated_seq INTEGER DEFAULT 0,
                    PRIMARY KEY(household_id, plan_id)
                );
                CREATE TABLE IF NOT EXISTS occurrences(
                    household_id INTEGER NOT NULL,
                    occurrence_id INTEGER PRIMARY KEY,
                    plan_id INTEGER NOT NULL,
                    title TEXT, dose TEXT DEFAULT '', note TEXT DEFAULT '',
                    due_ts INTEGER NOT NULL,
                    state TEXT DEFAULT 'pending',
                    snooze_count INTEGER DEFAULT 0,
                    snooze_until_ts INTEGER DEFAULT 0,
                    window_min INTEGER DEFAULT 30,
                    escalate_after_min INTEGER DEFAULT 15,
                    updated_seq INTEGER DEFAULT 0,
                    last_event_id TEXT DEFAULT ''
                );
                CREATE TABLE IF NOT EXISTS events(
                    household_id INTEGER NOT NULL,
                    event_id TEXT PRIMARY KEY,
                    occurrence_id INTEGER,
                    action TEXT NOT NULL,
                    ts INTEGER NOT NULL,
                    device_id TEXT DEFAULT '',
                    reason TEXT DEFAULT '',
                    server_seq INTEGER NOT NULL,
                    applied INTEGER DEFAULT 0
                );
                CREATE TABLE IF NOT EXISTS records(
                    household_id INTEGER NOT NULL,
                    record_id INTEGER NOT NULL,
                    type TEXT, value REAL, unit TEXT,
                    ts INTEGER NOT NULL,
                    updated_seq INTEGER DEFAULT 0,
                    client_id TEXT DEFAULT '',
                    PRIMARY KEY(household_id, record_id)
                );
                CREATE TABLE IF NOT EXISTS alarms(
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    household_id INTEGER NOT NULL,
                    occurrence_id INTEGER,
                    source TEXT DEFAULT 'auto',
                    level INTEGER DEFAULT 1,
                    ts INTEGER NOT NULL,
                    device_id TEXT DEFAULT ''
                );
                """
            )
            # 旧库补列：records.client_id（端上 POST 重试去重用）
            cols = {r["name"] for r in self.db.execute("PRAGMA table_info(records)")}
            if "client_id" not in cols:
                self.db.execute("ALTER TABLE records ADD COLUMN client_id TEXT DEFAULT ''")

    # ---- 序号 ----

    def bump_seq(self, household_id: int) -> int:
        self.db.execute(
            "UPDATE households SET seq = seq + 1 WHERE id = ?", (household_id,))
        return self.db.execute(
            "SELECT seq FROM households WHERE id = ?", (household_id,)).fetchone()["seq"]

    # ---- 配对 ----

    def pair(self, code: str, role: str, device_id: str):
        with self.lock, self.db:
            row = self.db.execute(
                "SELECT id FROM households WHERE pair_code = ?", (code,)).fetchone()
            if row is None:
                # 联调约定：任意 6 位配对码首次使用即建档；生产环境应改为预登记。
                cur = self.db.execute(
                    "INSERT INTO households(pair_code) VALUES (?)", (code,))
                household_id = cur.lastrowid
            else:
                household_id = row["id"]
            token = uuid.uuid4().hex
            self.db.execute(
                "INSERT INTO devices(household_id, device_id, role, token) VALUES (?,?,?,?) "
                "ON CONFLICT(household_id, device_id) DO UPDATE SET token = excluded.token,"
                " role = excluded.role",
                (household_id, device_id, role, token))
        return household_id, token

    def household_by_token(self, token: str):
        row = self.db.execute(
            "SELECT h.* FROM households h JOIN devices d ON d.household_id = h.id "
            "WHERE d.token = ?", (token,)).fetchone()
        return dict(row) if row else None

    # ---- 计划 ----

    def upsert_plan(self, household_id: int, plan: dict) -> int:
        """新建或修改，返回 updated_seq。plan 为规范 §8 的 snake_case 形状。"""
        with self.lock, self.db:
            seq = self.bump_seq(household_id)
            existing = self.db.execute(
                "SELECT version FROM plans WHERE household_id=? AND plan_id=?",
                (household_id, plan["plan_id"])).fetchone()
            version = (existing["version"] + 1) if existing else 1
            self.db.execute(
                "INSERT INTO plans(household_id, plan_id, title, dose, note, rule_json,"
                " window_min, escalate_after_min, allow_late, skip_dates, version, deleted,"
                " updated_seq) VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?) "
                "ON CONFLICT(household_id, plan_id) DO UPDATE SET title=excluded.title,"
                " dose=excluded.dose, note=excluded.note, rule_json=excluded.rule_json,"
                " window_min=excluded.window_min, escalate_after_min=excluded.escalate_after_min,"
                " allow_late=excluded.allow_late, skip_dates=excluded.skip_dates,"
                " version=excluded.version, deleted=excluded.deleted,"
                " updated_seq=excluded.updated_seq",
                (household_id, plan["plan_id"], plan.get("title", ""), plan.get("dose", ""),
                 plan.get("note", ""), json.dumps(plan.get("rule", {}), ensure_ascii=False),
                 plan.get("window_min", 30), plan.get("escalate_after_min", 15),
                 1 if plan.get("allow_late", True) else 0,
                 json.dumps(plan.get("skip_dates", []), ensure_ascii=False),
                 version, 1 if plan.get("deleted", False) else 0, seq))
            return seq

    def plan_row(self, household_id: int, plan_id: int):
        """读一条计划（snake_case，规则/跳过日期已解析）；不存在返回 None。"""
        row = self.db.execute(
            "SELECT * FROM plans WHERE household_id=? AND plan_id=?",
            (household_id, plan_id)).fetchone()
        if row is None:
            return None
        return {
            "plan_id": row["plan_id"], "title": row["title"], "dose": row["dose"],
            "note": row["note"], "rule": json.loads(row["rule_json"] or "{}"),
            "window_min": row["window_min"], "escalate_after_min": row["escalate_after_min"],
            "allow_late": bool(row["allow_late"]),
            "skip_dates": json.loads(row["skip_dates"] or "[]"),
            "version": row["version"], "deleted": bool(row["deleted"]),
        }

    def next_plan_id(self, household_id: int) -> int:
        with self.lock, self.db:
            pid = self.db.execute(
                "SELECT next_plan_id FROM households WHERE id=?",
                (household_id,)).fetchone()["next_plan_id"]
            self.db.execute("UPDATE households SET next_plan_id=? WHERE id=?",
                            (pid + 1, household_id))
        return pid

    def plans_since(self, household_id: int, since: int):
        rows = self.db.execute(
            "SELECT * FROM plans WHERE household_id=? AND updated_seq>? ORDER BY plan_id",
            (household_id, since)).fetchall()
        out = []
        for r in rows:
            out.append({
                "plan_id": r["plan_id"], "title": r["title"], "dose": r["dose"],
                "note": r["note"], "rule": json.loads(r["rule_json"]),
                "window_min": r["window_min"], "escalate_after_min": r["escalate_after_min"],
                "allow_late": bool(r["allow_late"]),
                "skip_dates": json.loads(r["skip_dates"] or "[]"),
                "version": r["version"], "deleted": bool(r["deleted"]),
                "updated_seq": r["updated_seq"],
            })
        return out

    # ---- occurrence 生成与自动迁移 ----

    @staticmethod
    def due_minutes(rule: dict, day: date):
        """返回该 rule 在 day 的应做分钟列表（规范 §6）。"""
        kind = rule.get("kind")
        times = rule.get("times") or []
        if kind == "daily":
            pass
        elif kind == "weekly":
            weekdays = rule.get("weekdays") or list(range(1, 8))
            if day.isoweekday() not in weekdays:
                return []
        elif kind == "monthly":
            d = min(rule.get("day", 1),
                    (date(day.year + (day.month == 12), day.month % 12 + 1, 1)
                     - timedelta(days=1)).day)
            if day.day != d:
                return []
        elif kind == "once":
            at = rule.get("at", "")
            if at[:10] != day.isoformat():
                return []
            times = [at[11:16]] if len(at) >= 16 else []
        else:
            return []
        out = []
        for t in times:
            m = re.match(r"^(\d{1,2}):(\d{2})$", t.strip())
            if m:
                out.append(int(m.group(1)) * 60 + int(m.group(2)))
        return sorted(out)

    def ensure_today(self, household_id: int):
        """跨天重建 occurrence + 自动迁移。每次 state/plans 请求前调用。"""
        with self.lock, self.db:
            hh = self.db.execute("SELECT * FROM households WHERE id=?",
                                 (household_id,)).fetchone()
            today = today_str()
            if hh["day"] != today:
                self.db.execute("DELETE FROM occurrences WHERE household_id=?",
                                (household_id,))
                self.db.execute("UPDATE households SET day=? WHERE id=?",
                                (today, household_id))
                day = date.today()
                plans = self.db.execute(
                    "SELECT * FROM plans WHERE household_id=? AND deleted=0",
                    (household_id,)).fetchall()
                for p in plans:
                    rule = json.loads(p["rule_json"])
                    if today in json.loads(p["skip_dates"] or "[]"):
                        continue
                    for idx, minute in enumerate(self.due_minutes(rule, day)):
                        due = int(datetime(day.year, day.month, day.day).timestamp() * 1000
                                  + minute * 60 * 1000)
                        self.db.execute(
                            "INSERT OR IGNORE INTO occurrences(household_id, occurrence_id,"
                            " plan_id, title, dose, note, due_ts, state, window_min,"
                            " escalate_after_min, updated_seq) VALUES (?,?,?,?,?,?,?,?,?,?,0)",
                            (household_id,
                             occurrence_id_for(p["plan_id"], day, idx), p["plan_id"],
                             p["title"], p["dose"], p["note"], due, "pending",
                             p["window_min"], p["escalate_after_min"]))

            # 自动迁移：过窗口 → missed；推迟到点 → notified（盖 updated_seq）
            now = now_ms()
            for occ in self.db.execute(
                    "SELECT * FROM occurrences WHERE household_id=?", (household_id,)).fetchall():
                deadline = max(occ["due_ts"], occ["snooze_until_ts"]) \
                    + occ["window_min"] * 60_000
                new_state = None
                if occ["state"] in ("pending", "notified", "snoozed"):
                    if occ["state"] == "snoozed" and now < occ["snooze_until_ts"]:
                        continue  # 还在推迟窗口里
                    if now > deadline:
                        new_state = "missed"
                if new_state:
                    seq = self.bump_seq(household_id)
                    self.db.execute(
                        "UPDATE occurrences SET state=?, updated_seq=? WHERE occurrence_id=?",
                        (new_state, seq, occ["occurrence_id"]))

    def occurrences_since(self, household_id: int, since: int):
        rows = self.db.execute(
            "SELECT * FROM occurrences WHERE household_id=? AND updated_seq>?"
            " ORDER BY due_ts", (household_id, since)).fetchall()
        return [{
            "occurrence_id": r["occurrence_id"], "plan_id": r["plan_id"],
            "title": r["title"], "dose": r["dose"], "note": r["note"],
            "due_ts": r["due_ts"], "state": r["state"],
            "snooze_count": r["snooze_count"], "snooze_until_ts": r["snooze_until_ts"],
            "window_min": r["window_min"], "escalate_after_min": r["escalate_after_min"],
            "updated_seq": r["updated_seq"], "last_event_id": r["last_event_id"],
        } for r in rows]

    # ---- 事件应用 ----

    def apply_event(self, household_id: int, ev: dict):
        """应用一条事件。返回 (accepted, applied, server_seq, reject_reason)。"""
        event_id = ev.get("event_id") or ""
        action = ev.get("action") or ""
        if not event_id or action not in VALID_ACTIONS:
            return (True, False, 0, "bad_event")
        with self.lock, self.db:
            dup = self.db.execute(
                "SELECT server_seq, applied FROM events WHERE event_id=?",
                (event_id,)).fetchone()
            if dup:
                return (True, bool(dup["applied"]), dup["server_seq"], "")
            if not self.db.execute(
                    "SELECT 1 FROM occurrences WHERE household_id=? AND occurrence_id=?",
                    (household_id, ev.get("occurrence_id") or 0)).fetchone():
                if action not in ("sos", "pair"):  # SOS/配对没有 occurrence
                    return (True, False, 0, "occurrence_not_found")

            seq = self.bump_seq(household_id)
            applied = self._transition(household_id, ev, seq)
            self.db.execute(
                "INSERT INTO events(household_id, event_id, occurrence_id, action, ts,"
                " device_id, reason, server_seq, applied) VALUES (?,?,?,?,?,?,?,?,?)",
                (household_id, event_id, ev.get("occurrence_id") or 0, action,
                 ev.get("ts") or now_ms(), ev.get("device_id") or "",
                 ev.get("reason") or "", seq, 1 if applied else 0))
        return (True, applied, seq, "" if applied else "stale_state")

    def _transition(self, household_id: int, ev: dict, seq: int) -> bool:
        """按规范 §5 迁移 occurrence 状态；返回是否发生了迁移。"""
        occ_id = ev.get("occurrence_id") or 0
        action = ev["action"]
        if action in ("sos", "pair"):
            return True
        occ = self.db.execute(
            "SELECT * FROM occurrences WHERE household_id=? AND occurrence_id=?",
            (household_id, occ_id)).fetchone()
        if occ is None:
            return False
        state, new = occ["state"], None
        if action == "fire" and state == "pending":
            new = "notified"
        elif action == "confirm" and state in ("notified", "snoozed", "missed", "escalated"):
            # allowLate=false 的计划 missed 后不可补做（规范 §2.4 late_not_allowed）
            plan = self.db.execute(
                "SELECT allow_late FROM plans WHERE household_id=? AND plan_id=?",
                (household_id, occ["plan_id"])).fetchone()
            if state == "missed" and plan and not plan["allow_late"]:
                return False
            new = "confirmed"
        elif action == "snooze" and state == "notified" and occ["snooze_count"] < MAX_SNOOZE:
            new = "snoozed"
        elif action == "undo" and state in ("confirmed", "snoozed", "missed", "escalated"):
            new = "notified"
        elif action == "undo" and state == "notified":
            new = "pending"
        elif action == "miss" and state in ("pending", "notified", "snoozed"):
            new = "missed"
        elif action == "escalate" and state in ("notified", "snoozed", "missed"):
            new = "escalated"
        if new is None:
            return False
        snooze_count = occ["snooze_count"] + (1 if action == "snooze" else 0)
        snooze_until = (ev.get("ts") or now_ms()) + SNOOZE_MS if action == "snooze" \
            else (0 if action in ("undo", "confirm") and new in ("pending", "notified")
                  else occ["snooze_until_ts"])
        self.db.execute(
            "UPDATE occurrences SET state=?, snooze_count=?, snooze_until_ts=?,"
            " updated_seq=?, last_event_id=? WHERE occurrence_id=?",
            (new, snooze_count, snooze_until, seq, ev.get("event_id") or "", occ_id))
        return True

    def next_seq(self, household_id: int) -> int:
        return self.db.execute("SELECT seq FROM households WHERE id=?",
                               (household_id,)).fetchone()["seq"]

    # ---- 记录 / 告警 ----

    def add_record(self, household_id: int, rec: dict):
        client_id = str(rec.get("client_id") or "")
        with self.lock, self.db:
            if client_id:
                # 端上 POST 超时重试：同一 client_id 只记一条，回原来的 id
                dup = self.db.execute(
                    "SELECT record_id, updated_seq FROM records"
                    " WHERE household_id=? AND client_id=?",
                    (household_id, client_id)).fetchone()
                if dup:
                    return dup["record_id"], dup["updated_seq"]
            rid = self.db.execute("SELECT next_record_id FROM households WHERE id=?",
                                  (household_id,)).fetchone()["next_record_id"]
            self.db.execute("UPDATE households SET next_record_id=? WHERE id=?",
                            (rid + 1, household_id))
            seq = self.bump_seq(household_id)
            self.db.execute(
                "INSERT INTO records(household_id, record_id, type, value, unit, ts,"
                " updated_seq, client_id) VALUES (?,?,?,?,?,?,?,?)",
                (household_id, rid, rec.get("type", ""), rec.get("value", 0),
                 rec.get("unit", ""), rec.get("at") or rec.get("ts") or now_ms(), seq,
                 client_id))
        return rid, seq

    def patch_record(self, household_id: int, record_id: int, patch: dict):
        with self.lock, self.db:
            cur = self.db.execute(
                "SELECT * FROM records WHERE household_id=? AND record_id=?",
                (household_id, record_id)).fetchone()
            if cur is None:
                return None
            seq = self.bump_seq(household_id)
            self.db.execute(
                "UPDATE records SET type=?, value=?, unit=?, ts=?, updated_seq=?"
                " WHERE household_id=? AND record_id=?",
                (patch.get("type", cur["type"]), patch.get("value", cur["value"]),
                 patch.get("unit", cur["unit"]),
                 patch.get("at") or patch.get("ts") or cur["ts"], seq,
                 household_id, record_id))
        return seq

    def add_alarm(self, household_id: int, body: dict):
        with self.lock, self.db:
            cur = self.db.execute(
                "INSERT INTO alarms(household_id, occurrence_id, source, level, ts,"
                " device_id) VALUES (?,?,?,?,?,?)",
                (household_id, body.get("occurrence_id") or 0,
                 body.get("source") or "auto", body.get("level") or 1,
                 body.get("ts") or now_ms(), body.get("device_id") or ""))
            alarm_id = cur.lastrowid
            # 带 occurrence 的告警：直接把状态推到 escalated（家人端看得见）
            if body.get("occurrence_id"):
                seq = self.bump_seq(household_id)
                self.db.execute(
                    "UPDATE occurrences SET state=?, updated_seq=?"
                    " WHERE household_id=? AND occurrence_id=? AND state"
                    " IN ('notified','snoozed','missed')",
                    ("escalated", seq, household_id, body["occurrence_id"]))
        return alarm_id


# ---------------------------------------------------------------- HTTP

class Handler(BaseHTTPRequestHandler):
    store: Store = None
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):  # 联调日志走 stdout，一条一行
        print("[%s] %s" % (datetime.now().strftime("%H:%M:%S"), fmt % args))

    # ---- 基础工具 ----

    def _send(self, status: int, payload: dict):
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _body(self) -> dict:
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0:
            return {}
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except Exception:
            return {}

    def _household(self):
        auth = self.headers.get("Authorization") or ""
        token = auth[7:] if auth.startswith("Bearer ") else ""
        return self.store.household_by_token(token) if token else None

    def _route(self, method: str):
        url = urlparse(self.path)
        path = url.path.rstrip("/")
        query = {k: v[0] for k, v in parse_qs(url.query).items()}

        if method == "POST" and path == "/v1/pair":
            return self._pair(self._body())

        hh = self._household()
        if hh is None:
            return self._send(401, {"error": "unauthorized"})

        if method == "POST" and path == "/v1/events":
            return self._events(hh, self._body())
        if method == "GET" and path == "/v1/state":
            return self._state(hh, query)
        if method == "GET" and path == "/v1/plans":
            return self._plans(hh, query)
        if method == "POST" and path == "/v1/alarm":
            return self._alarm(hh, self._body())
        if method == "POST" and path == "/v1/plans":
            return self._create_plan(hh, self._body())
        m = re.match(r"^/v1/plans/(\d+)$", path)
        if m and method in ("PATCH", "DELETE"):
            return self._modify_plan(hh, int(m.group(1)), method, self._body())
        if method == "POST" and path == "/v1/records":
            return self._create_record(hh, self._body())
        m = re.match(r"^/v1/records/(\d+)$", path)
        if m and method == "PATCH":
            return self._patch_record(hh, int(m.group(1)), self._body())

        self._send(404, {"error": "not_found"})

    def do_GET(self):
        self._route("GET")

    def do_POST(self):
        self._route("POST")

    def do_PATCH(self):
        self._route("PATCH")

    def do_DELETE(self):
        self._route("DELETE")

    # ---- 端点 ----

    def _pair(self, body: dict):
        # 契约键名 pair_code（CareMate 后端同款）；旧键 code 兼容保留
        code = str(body.get("pair_code") or body.get("code") or "").strip()
        role = body.get("role") or "elder"
        device_id = body.get("device_id") or ""
        if not re.match(r"^\d{6}$", code) or not device_id:
            return self._send(403, {"error": "bad_pair_code"})
        hh_id, token = self.store.pair(code, role, device_id)
        hh = self.store.db.execute("SELECT * FROM households WHERE id=?",
                                   (hh_id,)).fetchone()
        self._send(200, {
            "token": token, "role": role, "device_id": device_id,
            "elder_name": hh["elder_name"] or "爷爷",
            "caregiver_name": hh["caregiver_name"] or "家人",
            "caregiver_phone": hh["caregiver_phone"] or "",
        })

    def _events(self, hh: dict, body: dict):
        accepted, rejected = [], []
        for ev in body.get("events") or []:
            ok, applied, seq, reason = self.store.apply_event(hh["id"], ev)
            if ok and not reason:
                accepted.append({"event_id": ev.get("event_id"),
                                 "server_seq": seq, "applied": bool(applied)})
            else:
                rejected.append({"event_id": ev.get("event_id"), "reason": reason})
        self._send(200, {"accepted": accepted, "rejected": rejected,
                         "next_seq": self.store.next_seq(hh["id"])})

    def _state(self, hh: dict, query: dict):
        self.store.ensure_today(hh["id"])
        since = int(query.get("since") or 0)
        self._send(200, {
            "next_seq": self.store.next_seq(hh["id"]),
            "server_time": now_ms(),
            "occurrences": self.store.occurrences_since(hh["id"], since),
        })

    def _plans(self, hh: dict, query: dict):
        self.store.ensure_today(hh["id"])
        since = int(query.get("since") or 0)
        self._send(200, {"next_seq": self.store.next_seq(hh["id"]),
                         "plans": self.store.plans_since(hh["id"], since)})

    def _alarm(self, hh: dict, body: dict):
        alarm_id = self.store.add_alarm(hh["id"], body)
        # 契约 §8：告警 ack 必须带 server_seq（端上用它摘掉待上传事件）
        seq = self.store.bump_seq(hh["id"])
        self._send(200, {"ok": True, "alarm_id": alarm_id, "server_seq": seq})

    def _create_plan(self, hh: dict, body: dict):
        plan = dict(body)
        plan["plan_id"] = body.get("plan_id") or self.store.next_plan_id(hh["id"])
        seq = self.store.upsert_plan(hh["id"], plan)
        version = self.store.plan_row(hh["id"], plan["plan_id"])["version"]
        self._send(200, {"plan_id": plan["plan_id"], "version": version, "server_seq": seq})

    def _modify_plan(self, hh: dict, plan_id: int, method: str, body: dict):
        # 在原计划上合并：DELETE 只改删除标记，PATCH 只改带来的字段
        # （以前整条覆盖，DELETE 会把标题/规则清空）
        cur = self.store.plan_row(hh["id"], plan_id)
        if cur is None:
            return self._send(404, {"error": "plan_not_found"})
        merged = dict(cur)
        if method == "DELETE":
            merged["deleted"] = True
        else:
            merged.update({k: v for k, v in body.items() if k != "plan_id"})
            merged["deleted"] = bool(body.get("deleted", False))  # PATCH 即恢复启用
        merged["plan_id"] = plan_id
        seq = self.store.upsert_plan(hh["id"], merged)
        version = self.store.plan_row(hh["id"], plan_id)["version"]
        self._send(200, {"plan_id": plan_id, "version": version, "server_seq": seq})

    def _create_record(self, hh: dict, body: dict):
        rid, seq = self.store.add_record(hh["id"], body)
        self._send(200, {"record_id": rid, "server_seq": seq})

    def _patch_record(self, hh: dict, record_id: int, body: dict):
        seq = self.store.patch_record(hh["id"], record_id, body)
        if seq is None:
            return self._send(404, {"error": "record_not_found"})
        self._send(200, {"record_id": record_id, "server_seq": seq})


def main():
    parser = argparse.ArgumentParser(description="颐伴联调服务端")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--db", default="yiban.db")
    parser.add_argument("--seed", default="888888",
                        help="预登记的配对码（逗号分隔）；留空则不预登记")
    parser.add_argument("--elder", default="爷爷")
    parser.add_argument("--caregiver", default="家人")
    parser.add_argument("--phone", default="")
    args = parser.parse_args()

    store = Store(args.db)
    for code in filter(None, args.seed.split(",")):
        store.db.execute(
            "INSERT OR IGNORE INTO households(pair_code, elder_name, caregiver_name,"
            " caregiver_phone) VALUES (?,?,?,?)",
            (code.strip(), args.elder, args.caregiver, args.phone))
        store.db.commit()

    Handler.store = store
    server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    print("颐伴联调服务端 http://0.0.0.0:%d/v1  （数据库 %s）" % (args.port, args.db))
    print("配对码：%s（任意 6 位数字也可首次建档）" % args.seed)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")


if __name__ == "__main__":
    main()
