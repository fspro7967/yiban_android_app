# S1：本地落库（SQLite）与持久化说明

> 日期：2026-11-18　负责人：B1（何奕辰）
> 依据：`docs/后端交接清单.md` §2.1（B1 必做）、`docs/端上接口规范_v1.1.md` §3（数据形状）/ §5（状态机）/ §7（事件）
> 范围：只改 `AppCore` 内部存储，**QML、6 个 facade、对外 QVariantMap 形状全部不动**

---

## 1. 这次做了什么

把 `AppCore` 的**真实数据域**从纯内存改成 **SQLite 落盘 + 启动恢复**，让「新建提醒 / 记录 / 紧急联系人」
在杀掉进程、重启手机之后仍然在；演示域（`demoMode`）保持内存快照，重启回到演示初始态。

具体三件事：

1. 打开本地库并建表（`plans` / `occurrences` / `records` / `events` / `meta` 五张表）；
2. 启动时把真实域快照从库里读回内存（失败或空库则从"新装状态"开始）；
3. 真实域每次变更后，在同一轮事件循环末尾把**整份快照**写回库（事务内先清表再插入）。

## 2. 为什么不是"读路径全改 SQL"

第一版（分支 `feat/s1-sqlite`）走的是"所有读路径改 SQL 查询"。合并 `main` 时发现上游 F1 在
`AppCore` 上新增了一整套**演示域**（真实/演示两份 `DataSnapshot` 互相切换）、操作日志、
撤销删除 / 跳过今天、跨天归档——这些语义都建立在"内存快照可整体替换"之上，
和"逐条 SQL 查询"是两种结构，硬接会把两边的代码搅在一起，风险高、收益低。

这一版的选择：**状态机逻辑照旧（上游那份不动），只在旁边加一层落盘**。
好处是 `tst_app_core` 的 24 个契约用例零改动通过；代价是读路径暂时仍走内存，
SQL 只承担"持久化 + 给同步队列留字段"。等 B2 的同步队列接上、演示域要不要落库定了，
再决定是否把热点查询下沉到 SQL。

## 3. 数据库设计

| 项 | 值 |
| --- | --- |
| 数据库文件 | `QStandardPaths::AppDataLocation/app_core.sqlite`（Android 上是应用私有目录） |
| 覆盖方式 | 环境变量 `YIBAN_APP_DB=<路径>`（单测隔离用） |
| 连接名 | `yiban_app_core`（`QSQLITE` 驱动，Desktop 与 Android 都随 Qt 提供） |
| 日志模式 | `journal_mode=WAL`、`synchronous=NORMAL` |

### 表结构

```sql
meta(key TEXT PRIMARY KEY, value TEXT)
-- schema_version / next_plan_id / next_record_id / next_event_seq
-- elder_name / caregiver_name / caregiver_phone / start_date / day
-- history / deleted_occurrences / skipped_occurrences（JSON）/ auto_reminders / saved_at_ms

plans(id INTEGER PRIMARY KEY, json TEXT NOT NULL)

occurrences(id INTEGER PRIMARY KEY, plan_id INTEGER, day TEXT, state TEXT, json TEXT NOT NULL)
-- 索引：idx_occurrences_day(day)

records(id INTEGER PRIMARY KEY, json TEXT NOT NULL)

events(event_id TEXT PRIMARY KEY, occurrence_id INTEGER, action TEXT, ts INTEGER,
       synced INTEGER NOT NULL DEFAULT 0, json TEXT NOT NULL)
-- 索引：idx_events_synced(synced)；synced 列是给 B2 上传重试用的占位
```

`json` 列里存的是规范 §3 那份 `QVariantMap` 原文（compact JSON），
所以对外形状永远和内存实现一致，改字段不用改表。
`occurrences` 的 `day / state` 和 `events` 的 `synced` 单独成列，是为了以后按天清理、
按待同步筛选时能走索引，不必解析 JSON。

## 4. 写路径

- 触发点：`occurrencesChanged` / `plansChanged` / `recordsChanged` / `profileChanged`
  四个信号 → `schedulePersist()`（`QTimer::singleShot(0)` 合并同一轮里的多次变更）→ `persistNow()`。
- `persistNow()` 在一个事务里 `DELETE` 五张表再整份插入，最后写 `meta`；
  失败就 `rollback()` 并打警告，不会留下半截数据。
- `setAutoReminders()` 不发射信号，单独调了一次 `schedulePersist()`。
- 演示模式（`demoMode == true`）时直接跳过写库：演示数据不落盘、也不会覆盖真实域。

## 5. 启动恢复

`AppCore` 构造时：建表 → `loadRealData()` →
- 有数据：`applySnapshot(真实域)`，跨天的话先按 `archiveDay()` 归档昨天再重建今天；
- 无数据（首次安装）：`clearActive()`，真实域空白，`startDate = 今天`。

## 6. 测试与验证

- `tests/tst_app_core.cpp` 的 `init()` 增加一行 `resetStorage()`：每条用例从空库开始，
  避免上一次运行（或上一次用例）的库里残留数据串进来；24 个断言一个没改。
- `tests/CMakeLists.txt`：`tst_app_core` 补链 `Qt6::Sql`，并给 ctest 设
  `YIBAN_APP_DB=<构建目录>/tst_app_core.sqlite`，不碰用户真实数据。
- `resetStorage()` 同时作为"清除本机数据"的公开入口（管理后台以后可以直接调）。

已验证：

| 场景 | 结果 |
| --- | --- |
| 桌面 `ctest` | 4/4 通过（含 `facade_app_core` 24 例） |
| 两个进程先后读写同一库 | 第一个进程建计划/记 2 杯水/存档案，第二个进程全部读回 |
| Android 模拟器（x86_64，API 37） | 建 18:00「吃药」→ `am force-stop` 杀进程 → 重开，首页显示「18:00 吃药 · 0/1」 |

## 7. 还没做 / 下一步

1. **同步队列**：`SyncVM::retrySync()` 仍是假实现，需要 `AppCore` 补
   `pendingEvents()` / `markSynced(ids)`，再让 `SyncVM` 发 `POST /v1/events`；
   `events.synced` 列已经备好。
2. **操作日志（`operationLog`）** 目前只在内存，重启会清空；管理后台要看历史的话需要落表。
3. **热点查询下沉 SQL**：统计、按天筛选、事件分页，现在都是内存算；数据量大了再迁。
4. **演示域是否也落盘**：现在演示域重启即重置（符合 README 的约定）；如果产品要求演示域的改动
   也保留，需要在同一套表上加 `domain` 列。

---

## 8. 2026-11-23 增量：同步队列、事件上限、升级链

按 `agent/1118TODO.md` 里 B1 的 P0 清单做完三件，接口定义同步登记到 `agent/接口冻结清单.md` 附录 A。

### 8.1 I1 同步队列（`AppCore`）

签名与 B2 分支（`feat_B2_platform`，11-22）里 `SyncVM` 已经在调的保持一致：

- `pendingEvents()`：只返回 `serverSeq == 0` 的事件；
- `markEventsSynced(acked)`：`[{eventId, serverSeq}]` 回写，重复 ack 幂等；
- `expireEvents(ids)`：服务端拒绝/409 冲突的事件置 `serverSeq = -1`，不再重传、留档可导出；
- `eventsChanged()` 信号：新事件入队、ack、被拒都会发；`SyncVM` 触发上传，落盘层触发写库；
- `SyncVM::pendingCount()` 改为只数 `serverSeq == 0`。

⚠️ 关键坑（TODO 点名的）：`persistNow()` 是按内存整份覆盖库的，
所以 `markEventsSynced` **必须同时改内存里的事件 map**，只 `UPDATE` 库会被下一次落盘冲掉。
本实现改的是 `m_events`，再由 `eventsChanged → schedulePersist` 异步落库。

### 8.2 事件超过 1000 条被删的问题

根因：`loadRealData` 里 `LIMIT 1000` + 每次落盘整份覆盖，每重启一次就丢掉最早的事件（含未上传的）。

修法：

- 读回不再截断（去掉 `LIMIT 1000`，也去掉无效的 `ORDER BY dueTs`）；
- 写库时裁剪：**待上传（`serverSeq == 0`）全留**，已处理（ack / -1）只留最近 800 条；
- 内存里同样设上限 2000，从最旧的"已处理"事件开始丢，pending 不动。

### 8.3 I2 升级链

- `AppCore` 在每条 occurrence 上维护 `notifiedAtTs` / `escalationLevel`；
- `onTick()` 按"提醒弹出后过了多久"推进：≥2 分钟 → 2 级（语音加强），
  ≥`plan.escalateAfterMin`（默认 15）→ 3 级，转 `escalated` 并记 `escalate(timeout)` 事件；
- 对外字段：`escalationLevel`（0–3）、`nextEscalationInSec`；
- 信号：`AppCore::escalationChanged(occurrenceId, level)`，`ReminderVM` 转发并刷新 `current`；
- 基线用 `notifiedAtTs` 而不是 `dueTs`：App 被杀一夜回来时不会一睁眼就跳到 3 级。

三级时"打电话/强提示"（`DeviceVM.sos(3)`）**没有**从数据层直接调用：
订阅 `escalationChanged(_, 3)` 由 QML 或 B2 的 `DeviceVM` 处理，避免 `AppCore` 反向依赖 `DeviceVM`。

### 8.4 可注入时钟与新增用例

`AppCore::setClockForTests(ms)` / `tickForTests()` 只影响本类的"现在/今天"，不碰系统时钟。
注意 `setClockForTests` **故意不改 `m_day`**：跨天必须由 `onTick` 自己发现（归档昨天 + 重建今天），
注入时顺手改会把要测的路径抄近路跳过。`createPlan/updatePlan/...` 里重置 `m_day` 的地方也都改成取注入时钟。

| 新增测试 | 覆盖 |
| --- | --- |
| `tests/tst_data_layer.cpp`（4 例） | 同步队列生命周期/幂等、升级链 1→2→3、跨天归档与统计 |
| `tests/tst_restart.cpp`（1 例，10 轮） | 同库连续 10 次"启动→校验→写一条→退出"，验证落盘与恢复 |

完整 `ctest`：**6/6 通过**（原 `facade_app_core` 24 条契约一字未改、原样通过）。

