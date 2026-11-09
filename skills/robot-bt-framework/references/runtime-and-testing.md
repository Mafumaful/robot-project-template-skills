# 执行器、诊断与测试

## 1. 执行器 mission_bt_node

- 组成：`NavigationBtRuntime`（加载/创建/tick 树）、`NavigationEventSource`（事件）、`TreeSnapshotPublisher`、`SpeechAnnouncer`（发后不管的播报）、`MotionGoalTracker`、`ButtonPauseGate`。
- 接口：
  - 服务 `/mission/start|stop|pause`（csr_msgs）
  - 话题 `/mission/goal_id`（JSON `{"action":"start","waypoint_ids":[...],"task_id":""}`，给语音/简单客户端）
  - 状态 `/mission/status`（IDLE/RUNNING/PAUSED/COMPLETED/FAILED，1 Hz）
  - `/service_events`、播报回退 `/interface/help_request`
- tick：`bt_tick_rate_hz=10`，独立 callback group。每次 tick：采样事件 → 锁内写事实到黑板 → `runtime->tick()` → 发快照 → 后处理（失败上下文非空则 FAILURE）。
- **树长寿命**：跨航点复用，`update_blackboard_for_waypoint()` 切换目标；停止用 `halt_tree()`。换树 = 用不同 `tree_file/main_tree_id` 重启（没有热替换）。
- 参数：`tree_directory`、`main_tree_id="UnifiedMissionRoot"`、`plugin_libraries`、`use_registered_tree`、`one_shot/autostart/stop_on_terminal`、`nav_retry_max=3`、`waypoint_task_timeout_sec=120`、`motion_cancel_timeout_sec=5`、`speech_say_action=/speech/say`、`text_speech_enabled`、`map_switching_enabled`。功能开关默认关，按需打开。
- 策略门 `NavigationBtPolicyGate`：用纯函数 `expected_phase(facts)` 和 XML 实际阶段比对，不一致 → haltTree + 诊断（`flow_consistent=false`）。
- 事件源：`/is_stuck` 只认 0→1 边沿；偏航 = 横向误差 > 1.8 m 持续 1 s；状态机无 ROS 依赖便于测试。

## 2. 诊断

| 话题/端口 | 内容 |
|---|---|
| `/navigation_bt/snapshot` | JSON，schema `navigation.bt.snapshot.v1`：`root_status, active_path, phase, outcome, target_kind, failure_kind, replan_attempt, arrival_verified, position_error, yaw_error, stable_samples, flow_consistent, tree_revision, nodes[]` |
| `/navigation_bt/events` | `navigation.bt.event.v1` |
| 8765 只读监控 | `/api/v1/bt/health|structure|snapshots/latest|snapshots|service-events`，**只有 GET** |
| 8767 沙箱 | 独立进程，`/api/v1/sandbox/state|snapshots|overrides|mock-facts`，只有这里允许 POST：覆盖条件结果（Auto/Success/Failure）、注入假事实 |

schema 有 C++ 校验函数（`validate_snapshot/validate_event/parse_overrides`），web 端和测试都用它。

## 3. 参数与 launch

- 每包一份 `config/*.yaml`，按节点名分节，中文分节注释（`# ---------- 导航 ----------`），关键参数旁写理由和事故日期。
- launch 参数只覆盖“每次部署不同”的输入（地图 yaml、输出目录、话题名）；其余都来自 yaml。
- 总入口 `unified_navigation.launch.py`：`start_xxx` 布尔开关 + `IfCondition`；`optional_include()` 遇到缺包/缺 launch 文件就跳过（但查不出缺可执行文件，所以部署顺序要保证依赖先装）。
- 生产值有校验脚本（`scripts/check_nav_params.py` 输出 `NAV_PARAMS_OK` 或差异）。

## 4. 测试分层

| 层 | 写法 |
|---|---|
| 纯逻辑 gtest | 无 ROS 的类：到达判定、避障计时、暂停门、事件状态机 |
| 节点 gtest | `SetUpTestSuite` 里 `rclcpp::init`；起 mock action/service server；黑板塞 `node/mutex/motion_tracker`；`factory.createTreeFromText("<root main_tree_to_execute=\"Main\"><BehaviorTree ID=\"Main\">"+xml+"</BehaviorTree></root>", bb)` 直接 tick |
| 生产树工厂测试 | 编译期宏传入真实插件 `.so` 路径和 `config/trees` 目录，加载全部 XML，断言能创建、端口都连上 |
| 契约测试（pytest） | 解析 launch 文件 AST/文本，钉死参数默认值和必须存在的参数；docstring 写清是防哪次事故 |
| 对齐测试 | 新旧实现跑同一组场景（parity scenarios） |
| 仿真验收 | Gazebo 无头 compose（独立 `ROS_DOMAIN_ID`）：preflight → run_mission → acceptance 脚本用 `jq` 轮询 8765：运行中、在动、非法目标被拒、堵路记忆+重规划、取消后速度归零、到达契约、播报；通过时打印固定一行 `... acceptance passed` |
| 真实数据回放 | 抓真机快照历史存成 `fixtures/*.real.json`（正例/反例），离线跑到达契约脚本 |

CMake：`ament_add_gtest(test_<unit> test/test_<unit>.cpp ...)` 链接 core 库；`ament_add_pytest_test` 跑 Python 契约。运行时排除 lint：

```bash
colcon test --packages-select <pkg> --ctest-args -E "(copyright|cppcheck|cpplint|flake8|pep257|uncrustify|xmllint|lint_cmake)"
```

lint 失败单独汇报，不和功能失败混在一起；把各包测试基线数（如 136/136）记在计划文档里。
