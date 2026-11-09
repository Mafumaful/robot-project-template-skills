---
name: robot-bt-framework
description: 机器人任务行为树（BehaviorTree.CPP v3 + ROS 2）框架范本，来自 robot 的 bt_xml_planner：XML 树如何分文件和命名、根节点 Stop/Pause/Active/Idle 优先级、黑板约定、插件节点（条件/同步/异步动作）写法、执行器(mission_bt_node)、到达契约、失败分类与恢复、快照/事件诊断、沙箱、gtest 与 Gazebo 验收。凡是新项目要用行为树编排机器人任务、导航、语音播报、多步流程、恢复策略，或要写/改 BT 节点、BT XML、任务执行器、BT 监控页面时，都要先用这个技能，哪怕用户只说“加个任务流程”“加个节点”“做个状态机”。
---

# 机器人行为树框架范本（bt_xml_planner）

源码：`robot/Traj_planning/bt_planner/`，三个 colcon 包：

| 包 | 内容 |
|---|---|
| `bt_xml_planner` | C++ 执行器 `mission_bt_node` + 插件库 `libbt_xml_planner_nodes.so` + `config/trees/*.xml` + 只读监控页(8765) + 沙箱(8767) + 测试 |
| `csr_msgs` | 接口：`StartMission/StopMission/PauseMission`、航点增删查、`MissionStatus`、沙箱 mock/override 服务 |
| `csr_task_scheduler` | Python：`waypoint_manager`、mock、`unified_navigation.launch.py` 总入口、`optional_launch.py` |

能力型兄弟包单独成包并只提供 action/service：规划/分段执行(`multi_map_switcher`：`/hpa_planner/plan_path`、`/hpa_executor/execute_segment`)、语音(`speech_interfaces`：`/speech/say`、`/speech/ask`)、视觉(`/analysis_camera` + VLM)。

## 核心理念（新项目必须继承）

1. **行为树拥有一切决策，能力节点只做能力**。原文：“HPA 只负责路线规划；切图、执行、失败决策和播报全部由 BT 收敛管理。”执行器/控制器里不允许有“差不多到了就算成功”的捷径。
2. **XML 管编排和数据流，插件管可复用行为，执行器只负责加载/初始化黑板/tick/发快照。**
3. **到达只由一个节点裁定**（`VerifyHpaGoalReached`）：动作服务器返回成功 ≠ 任务成功。契约：TF 年龄 ≤ 0.5 s、位置误差 ≤ 0.30 m、线速度 < 0.05 m/s 且角速度 < 0.05 rad/s、连续 3 个稳定样本；`align_yaw=false` 时朝向只报告不判定。被拒绝的目标永远不算到达。
4. **失败关闭（fail-closed）**：恢复分支返回 SUCCESS 不能让航点完成（根 SUCCESS 但 `failure_context` 非空 → 强制 FAILURE）；取消未确认(`CANCEL_UNCONFIRMED`)就停任务，绝不重复发目标；XML 阶段与策略预期不一致 → halt + 诊断。
5. **每个等待都有超时**：节点都有 `timeout_sec`/`server_timeout` 端口；`WaitUntilStopped 5 s`；避障停留时间(`/sa_mppi/obstacle_hold`)从分段超时里扣掉。
6. **播报永不阻塞导航**：`Speak` 默认 `required=false`，失败只告警；有 priority/dedup/interrupt 语义。
7. **可观测但不可误控**：生产监控页只读；能改条件结果、注入假事实的只有隔离的沙箱进程。

## 树的组织

- 目录 `config/trees/`，**一个文件一棵树**：文件名 snake_case 表角色，`BehaviorTree ID` 用 PascalCase，`BTCPP_format="3"`；启动时整目录注册，`SubTreePlus` 跨文件引用。
- 计算 `tree_revision = "fnv1a64:<文件名+内容哈希>"`，写进每个快照，便于确认机器人跑的是哪版树。
- 根树范式（`assets/trees/main_tree.xml`）：

```xml
<ReactiveFallback name="UnifiedMissionRoot">
  <Sequence name="StopBranch">   <IsStopRequested state="{stop_requested}" expected="true"/>
    <ForceSuccess><Sequence><CancelNavigation .../><WaitUntilStopped timeout_sec="5.0" .../></Sequence></ForceSuccess></Sequence>
  <Sequence name="PauseBranch">  <IsPauseRequested .../> 同上 </Sequence>
  <Sequence name="ActiveMissionBranch"><IsMissionActive state="{mission_running}" expected="true"/><SubTreePlus ID="..." .../></Sequence>
  <Sequence name="IdleBranch">   <IsMissionInactive/><IsPauseClear/><IsStopClear/></Sequence>
</ReactiveFallback>
```

  `ReactiveFallback` 保证每个 tick 先检查停止和暂停，优先级就是分支顺序。
- 任务子树是线性流水线：初始化 → 取位姿 → 规划 → 等代价地图 → 分段执行循环。
- 分段循环（`assets/trees/execute_segment_tree.xml`）：`Fallback( Repeat(-1, ReactiveFallback(外部阻塞恢复 | 执行当前段 | 瞬态重试 | 终点局部恢复 | 重规划)), IsRouteComplete )`。执行当前段 = 选段 → 滚动窗口 → 按需切图 → 等代价地图 → 执行动作 → **分类结果** → **验证到达** → 提交进度。
- 恢复子树（`navigation_recovery_tree.xml`）：外部阻塞 → 取消 → 等停稳 → 拍照 → VLM 判断 → 播报 → 判定堵了就记忆+重规划，否则清除标记。

### XML 书写规则

- 所有黑板绑定显式写 `port="{key}"`；`SubTreePlus` 把共享键**全部列出**（不要用 `__shared_blackboard`），数据流在 XML 里看得见。
- 节点实例 `name=` 在树内唯一——沙箱按 `leaf/<name>` 覆盖条件结果。
- 每个节点输出 `phase`/`detail` 到黑板，快照里就能看到“正在干什么、为什么”。
- 动作/服务名作为黑板静态键注入（`{navigate_to_pose_action}`），不在 XML 里硬编码生产名。

## 节点写法

详细模板和代码见 `references/node-patterns.md`，源码参考 `assets/cpp/`。要点：

| 种类 | 基类 | 规则 |
|---|---|---|
| 条件 | `BT::ConditionNode` | `tick()` 内锁黑板 mutex；结果经 `ConditionResultCallback("leaf/"+name())` 以便沙箱覆盖；一个类可注册多个 XML 标签（`IsBooleanState` → `IsStopRequested/IsPauseRequested/...`） |
| 瞬时动作 | `BT::SyncActionNode` | 只读写黑板，不阻塞 |
| 异步动作 | `BT::StatefulActionNode` | 构造时建 client（提前发现）；`onStart/onRunning` 返回 RUNNING；`onHalted()` 必须取消 goal；有 `server_timeout`；迟到的接受也要取消，避免孤儿 goal |

新增节点 6 步：头文件放 `include/<pkg>/bt_nodes/<domain>_nodes.hpp` → 实现 `src/<domain>_nodes.cpp` → 加到 CMake 的插件库 → 在 `src/bt_nodes.cpp` 的 `BT_REGISTER_NODES` 注册 → 写进 `config/bt_nodes.json`（端口目录，给编辑器/监控用）→ 写 XML 示例和 gtest。

## 执行器与诊断

见 `references/runtime-and-testing.md`：`mission_bt_node` 的服务/话题/参数、长寿命树、事件源、`ButtonPauseGate`、快照/事件 JSON schema、8765 只读监控与 8767 沙箱、四类测试与 Gazebo 验收。

## 新项目落地清单

1. 建 `<proj>_bt`（执行器+插件+trees）、`<proj>_msgs`、`<proj>_task_scheduler`（launch/mock）三个包。
2. 先写根树（Stop/Pause/Active/Idle）和 `bt_context.hpp` 的 `BlackboardKeys`（`node`、`blackboard_mutex`、`condition_result`…）。
3. 定义领域类型（失败种类枚举、上下文结构、JSON 序列化），放 `navigation_context.hpp` 一类头文件。
4. 每个能力一个 action/service + 一个异步 BT 节点；裁定类节点（到达、成功判断）独立且唯一。
5. 发布快照 `/<proj>_bt/snapshot`（schema `<proj>.bt.snapshot.v1`）和事件；做只读监控页。
6. 测试：纯逻辑 gtest → 节点 gtest（`createTreeFromText`）→ 加载真实插件+真实 XML 的工厂测试 → launch 契约测试 → 仿真验收脚本。

## 常见错误

| 错误 | 改为 |
|---|---|
| 在动作服务器里判断“到了” | 交给唯一的 Verify 节点，带稳定样本 |
| 恢复成功就算任务成功 | 根结果后处理：有失败上下文即 FAILURE |
| `onHalted()` 空实现 | 取消 goal/future，等确认 |
| 任务状态藏在私有 context 对象里 | 放黑板并在 XML 连线 |
| 播报失败让任务失败 | `required=false`，只告警 |
| 无限重试 | `nav_retry_max`（3）+ 阻塞记忆上限（8 条、3 次重规划） |
| 生产监控页可以 POST | 只读；写操作只在沙箱 |
