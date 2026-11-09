# 后端架构

## 1. server.py

- `ThreadingHTTPServer` + `BaseHTTPRequestHandler`；`Application` 持有 ROS 桥、作业管理器、参数工作室、`Console`（组合根）。
- **GET 链**：每个功能模块提供 `handle_get(handler, path) -> bool`，按顺序尝试（replay → legacy → module_debug → map3d → export → console_api），都没处理再走核心路由。
- **POST 流水线**（顺序就是安全检查顺序）：
  1. body 大小限制（64 KiB；地图编辑 4 MiB）
  2. 必须是 JSON 对象
  3. 页面 token：`secrets.compare_digest`
  4. `Origin` 必须等于 `Host`
  5. 登录/登出 + `auth.require(path, cookie)`（`OPEN_POSTS` 放行急停/停止/心跳等）
  6. 各模块 `handle_post`
  7. **demo 门**：`409 只读 UI 展示模式`
  8. 其余控制路由
- `reply()`：JSON > 2 KiB 时 gzip；`Cache-Control:no-store`、`X-Content-Type-Options:nosniff`、`X-Frame-Options:DENY`；`safe_json` 把 NaN/Inf 去掉。
- 异常映射：`ValueError/KeyError/TypeError → 400`，`PermissionError → 401 {error, login:true}`，其它 → 500；**错误消息就是中文异常文本**，所以抛异常时直接写给操作员看的话。
- `log_message` 屏蔽高频轮询路径，日志只留有意义的请求。
- CLI：`--host --port 8090 --workspace --output --data ~/robot_data --deadman --demo …`。

## 2. 模块划分

每个功能一个文件（100–800 行），顶部 docstring 说明职责并引用 spec 章节。典型：

| 层 | 文件 |
|---|---|
| 基础 | `core.py`（`atomic_json` 先写 tmp 再 replace、`validate_points`、`inside(root, rel)` 防目录穿越、`Recorder` 录包）、`runner.py`（唯一能跑外部命令/读 /proc 的地方）、`progress.py`、`auth.py`、`units.py`（渲染 systemd 单元） |
| 组合根 | `console_api.py`（路由 + `Console`）、`server.py` |
| 设备/系统 | `nodes.py`（systemctl --user、就绪检查、journal 提示）、`groups.py`（dataclass `Group/Check` 注册表）、`hardware.py`、`sysload.py`、`domain.py`、`robot_id.py`、`versions.py`、`ota.py` |
| 业务 | `maps.py / map_switch.py / map_edit.py / mapping.py / mapping_save.py / relocalize.py / locations.py / mission_control.py / teleop.py / max_speed.py / voice_nav.py / params.py` |
| 证据 | `export.py`、`replay.py / replay_extract.py / replay_bt.py`、`report.py` |
| 调试 | `module_debug.py`、`speech_debug.py`、`map3d.py` |
| ROS | `ros_bridge.py` |

## 3. 后台作业模型

```python
job = {'id': time.strftime('%Y%m%d_%H%M%S'), 'state': 'running', 'step': None, 'error': None,
       'started': now, 'ended': None}
progress.plan(job, ['检查', '停止定位', '切换地图', '启动定位', '重定位', '完成'])
def _set(**fields):            # 在锁内
    job.update(fields); progress.reach(job, fields.get('step'))
```

- 状态机：`running → succeeded | failed`，失败时 `rolling_back → rolled_back | failed`；回滚有自己 `plan()` 的步骤。
- `_claim(allowed_states, new_state)` 原子地抢占，防止两个页面同时发起。
- `status()` 返回深拷贝（`json.loads(json.dumps(job))`），读写都在锁内。
- 入口校验 `if body.get('confirm') is not True: raise ValueError('需要确认')`。
- 工作在 `threading.Thread(daemon=True)`；完成后追加 `~/<data>/log/<name>_history.jsonl`。
- HTTP 返回 `202 + status()`；前端用 `progress.job()` 画。
- 测试 `test_progress.py` 断言每个作业“计划里的每一步都真的到达过”。

## 4. ROS 桥线程模型

- 一个 `RosBridge(Node)` 跑在 `MultiThreadedExecutor(num_threads=4)` 的守护线程里；高频传感器（~400 msg/s）放第二个节点、单独的 `SingleThreadedExecutor` 线程，避免拖慢控制回调。
- 客户端/定时器用 `ReentrantCallbackGroup`。定时器示例：20 ms 安全 tick、100 ms 采样、2 s 生命周期/图、1 s 定位分数。
- 等 future 用自写的轮询 `wait(future, timeout, cancel)`（20 ms sleep），**从不在 HTTP 线程里 `spin_until_future_complete`**。
- **运行时不 destroy 常驻对象**（曾导致 spin 线程崩溃）：`destroy_subscription/destroy_client` 只用于请求级临时对象，常驻对象在 `close()` 统一清理。
- 重计算（例如地图索引重建）不能放在 ROS 回调里同步做——2026-11-05 曾因每次 /map 重建索引阻塞回调，触发了代价地图过期看门狗导致任务被停。

## 5. 鉴权（`assets/backend/auth.py`）

- PBKDF2-SHA256 20 万次；文件 `<data>/auth/{password,sessions.json,page_token}`，0600，tmp+replace 写入。
- Cookie `robot_session; HttpOnly; SameSite=Strict`，一年空闲期（现场设备登录一次就够）。
- 每 IP 每分钟最多 10 次登录尝试。
- `OPEN_POSTS`：急停、停止、心跳、登录/登出、松开摇杆、暂停、参数预览（只算 diff）——**安全动作永远不能被登录挡住**。
- 默认密码模板里改成环境变量 `CONSOLE_DEFAULT_PASSWORD`；每个项目自己定，首次登录后改。

## 6. 数据与部署

- 数据目录 `--data ~/robot_data`：`auth/`、`selection.json`、`maps/`、`log/`、`backup/`、缓存、各种小 json 配置；运行输出 `--output ~/nav_test_runs/<run_id>/`（含 `run.json`、录包、日志）。
- 代码目录在设备上是一个独立 checkout（`~/robot_console`）。
- `start_console.sh` 是 systemd 的 ExecStart：source ROS、读 domain id、导出 DDS 环境、`exec python3 server.py …`。
- `units.py` 渲染 systemd 用户单元：`robot-console.service`（`Restart=on-failure`）、每个节点组 `robot-<group>.service`（`KillMode=control-group`、`KillSignal=SIGINT`、`TimeoutStopSec=20`、**`Restart=no` 让崩溃被看见**，`Requires/After/Conflicts` 来自 `groups.py`），汇总到 `robot-stack.target`。
- `deploy/install_units.sh` 只安装和校验，不擅自 enable；`deploy/rollback_to_<old>.sh` 回到旧系统，按部署前节点列表快照做 diff。
- 只改静态文件（html/js/css）时**只替换静态文件、不重启服务**；改 Python 才重启。

## 7. 测试

- 标准库 `unittest`，`python3 -m unittest discover -s tests -v`；在 Mac 上用 `/usr/bin/python3`（Homebrew python 缺 yaml/PIL）。
- 每个模块 `tests/test_<module>.py`；`fakes.py` 提供 `Clock`、`FakeRunner.on(prefix, response)`（最长前缀匹配，response 可以是函数）；按需写 `FakeBridge/FakeManager/FakeHandler`。
- HTTP 测试真起一个 `ThreadingHTTPServer --demo`，验证 token/Origin/登录/demo 门。
- 结构性测试：解析 `debug-manuals.js` 断言每个调试模块都有手册；`test_progress.py` 断言计划步骤全部到达。
- 浏览器检查：`tests/*_browser_check.js` 用 CDP 驱动无头 Chrome，断言无 JS 异常、**没有发出任何运动类 POST**，输出 `*_report.json` + 截图，明暗主题各截一张。
- PR 里写测试总数（“682 个控制台测试通过”）。

## 8. 文档

- `README.md`（中文）：部署、现场检查、按阶段的变更记录（带日期）、接口列表、边界（“不是/不会”）、测试数。
- `COMMANDS.md`：旧系统命令 ↔ 新命令对照表。
- `VERIFICATION.md`：验证了什么、**没有证明什么**、截图绝对路径。
