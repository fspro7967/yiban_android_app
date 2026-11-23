# 颐伴联调服务端

按 [`docs/端上接口规范_v1.1.md`](../docs/端上接口规范_v1.1.md) 第 8 节实现的后端，
**单文件、零依赖**（Python 3.8+ 标准库 + sqlite3），给小组联调用。
数据落在本地 SQLite（默认 `server/yiban.db`），重启不丢。

## 快速开始

```bash
# 在任何装了 Python 3.8+ 的机器上
python server/app.py --port 8000

# 也可预登记配对码和档案
python server/app.py --port 8000 --seed 888888 --elder 爷爷 --caregiver 王一丞 --phone 13800005678
```

## 让手机连上

1. 手机和电脑连**同一个 Wi-Fi**；
2. 电脑查本机局域网 IP（Windows：`ipconfig` 里的 IPv4 地址）；
3. App 管理页 → 网络设置 → 服务器地址填 `http://<电脑IP>:8000/v1`，保存；
4. 配对码输任意 6 位数字（首次使用自动建档，联调方便；生产应预登记）；
5. 管理页"同步"区块：状态变 `idle`、待上传条数开始下降即接通。

> 手机上测过 HTTP 明文没问题（Manifest 已开 `usesCleartextTraffic`）。
> 电脑防火墙第一次会拦 Python，选"允许"。

## 端点一览

| 方法 | 路径 | 说明 |
| --- | --- | --- |
| POST | `/v1/pair` | 配对码换 token + 档案（`Authorization: Bearer` 之后必带） |
| POST | `/v1/events` | 事件上传，按 `event_id` 幂等去重（重试重放不重复记账） |
| GET | `/v1/state?since=N` | occurrence 增量（`next_seq` 游标） |
| GET | `/v1/plans?since=N` | 计划增量 |
| POST | `/v1/alarm` | 呼救/升级告警（手动 SOS 时 `occurrence_id` 可空） |
| POST / PATCH / DELETE | `/v1/plans[/{id}]` | 计划写（version+1；DELETE 为软删） |
| POST / PATCH | `/v1/records[/{id}]` | 记录写 |

响应形状与 `docs/fixtures/*.json` 一致（snake_case）。

## 服务端行为

* **server_seq 单调游标**：计划、occurrence 状态、记录的每次写入都盖
  `updated_seq`，端上用 `since=` 增量拉，只持久化"已应用到的 seq"。
* **occurrence 生命周期在服务端**：每天首次访问按计划 rule
  （daily/weekly/monthly/once）生成当天 occurrence（pending），过窗口自动转
  `missed`、推迟到点转回 `notified`；端上传来的事件按规范 §5 状态机迁移
  （confirm/snooze/undo/escalate，含 `late_not_allowed` 判断）。
* **冲突以服务端为准**（规范 §9）：端上拉到的新状态直接覆盖本地旧状态。

## curl 自检

```bash
# 配对拿 token
TOKEN=$(curl -s -X POST localhost:8000/v1/pair \
  -H "Content-Type: application/json" \
  -d '{"code":"888888","role":"elder","device_id":"dev1"}' | python -c "import json,sys;print(json.load(sys.stdin)['token'])")

# 建计划（服务端下发用）
curl -s -X POST localhost:8000/v1/plans -H "Authorization: Bearer $TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"title":"吃降压药","rule":{"kind":"daily","times":["08:00"]}}'

# 拉状态
curl -s "localhost:8000/v1/state?since=0" -H "Authorization: Bearer $TOKEN"
```

## 与客户端的联动

| 端上 | 服务端 |
| --- | --- |
| `SyncVM` 事件队列（`AppCore.pendingEvents`） | `POST /v1/events`，ack 回写 `serverSeq` |
| 15s 定时器 | `GET /v1/state?since=` → `applyServerOccurrences` |
| 60s 定时器 | `GET /v1/plans?since=` → `applyServerPlans` |
| `DeviceVM.pair` | `POST /v1/pair` → token 落盘 + 档案写入 |
| `DeviceVM` SOS/升级 | `POST /v1/alarm` |

## 已知边界（给后续迭代）

* 单机单进程 + 全局锁，联调规模（几台手机）没问题，压测/多实例需换真 DB 与 Web 框架；
* 没有鉴权粒度（任何 household 成员可读写全部数据），家人端隔离没做；
* 配对码"任意 6 位首次建档"只是联调便利，生产要预登记 + 一次性；
* 推送（规范里"启动 + 每 15s + 推送"）的推送部分未实现，目前靠轮询。
