# BT 节点模式

源文件拷贝在 `../assets/cpp/`：`bt_context.hpp`（黑板键）、`gate_nodes.cpp`（条件节点）、`speech_nodes.hpp`（异步 action 节点模板 `SpeechGoalNode<ActionT>`）。上游原文在 `upstream-plugin-guide.md`。

## 1. 黑板约定

```cpp
struct BlackboardKeys {
  static constexpr const char * node = "node";                    // rclcpp::Node*，执行器注入
  static constexpr const char * mutex = "blackboard_mutex";       // std::shared_ptr<std::mutex>
  static constexpr const char * motion_tracker = "motion_goal_tracker";
  static constexpr const char * condition_result = "condition_result";  // 沙箱覆盖回调
};
```

- 节点通过 `get_ros_node(config)` / `get_blackboard_mutex(config)` 取（`node_utils.hpp`），不要自己 new 节点。
- 执行器 `initialize_static_blackboard()` 写入：动作/服务名、布尔标志（`mission_running/pause_requested/stop_requested/road_blocked/off_route/external_block_pending`）、富类型（`route`、`cursor`、`hpa_session`、`navigation_context`）、字符串阶段（`hpa_phase="IDLE"`、`hpa_detail`）、到达字段。
- 富类型集中定义：`FailureKind {PLAN_FAILED, ROAD_BLOCKED, EXECUTION_FAILED, OFF_ROUTE, MAP_SWITCH_FAILED, LOCALIZATION_LOST, NAV2_SERVER_UNAVAILABLE, TIMEOUT, CANCELED}`、`FailureContext`、`VlmResult{BLOCKED/PASSABLE/UNKNOWN, confidence, ...}`、`BlockedRouteMemory{max_entries=8, max_replan_attempts=3}`，都带 JSON 序列化（快照要用）。
- 多节点共享的可变状态用带自己 mutex 的句柄对象（`HpaSessionHandle`），通过端口传递指针。

## 2. 条件节点

```cpp
BT::PortsList IsBooleanState::providedPorts() {
  return { BT::InputPort<bool>("state", "Boolean state from the blackboard"),
           BT::InputPort<bool>("expected", true, "Boolean value that returns SUCCESS") };
}
BT::NodeStatus IsBooleanState::tick() {
  std::lock_guard<std::mutex> lock(*blackboard_mutex_);
  bool result = current_state(*this) == expected_state(*this);
  try {
    auto cb = config().blackboard->get<ConditionResultCallback>(BlackboardKeys::condition_result);
    result = cb("leaf/" + name(), result);           // 沙箱可强制 Success/Failure
  } catch (const std::exception &) {}                // 独立运行时没有回调
  return result ? BT::NodeStatus::SUCCESS : BT::NodeStatus::FAILURE;
}
```

注册时一个类多个标签：

```cpp
BT_REGISTER_NODES(factory) {
  factory.registerNodeType<IsBooleanState>("IsStopRequested");
  factory.registerNodeType<IsBooleanState>("IsPauseRequested");
  factory.registerNodeType<IsBooleanState>("IsMissionActive");
  bt_xml_planner::register_speech_nodes(factory);   // 领域分组注册
}
```

## 3. 异步 action 节点骨架

```cpp
template<class ActionT>
class GoalNode : public BT::StatefulActionNode {
public:
  GoalNode(const std::string & name, const BT::NodeConfiguration & config, std::string default_action)
  : BT::StatefulActionNode(name, config), node_(get_ros_node(config)), default_action_(std::move(default_action)) {
    if (node_) client_ = rclcpp_action::create_client<ActionT>(node_, default_action_);  // 构造即发现
  }
  BT::NodeStatus onStart() override {
    getInput("server_timeout", server_timeout_sec_);           // 默认 1.0 s
    started_at_ = now(); goal_sent_ = false;
    if (!build_goal(goal_, error)) return BT::NodeStatus::FAILURE;
    return onRunning();
  }
  BT::NodeStatus onRunning() override {
    if (!goal_sent_) {
      if (!client_->action_server_is_ready())
        return elapsed(started_at_) > server_timeout_sec_ ? unavailable("server not available") : BT::NodeStatus::RUNNING;
      call_.send(client_, goal_); goal_sent_ = true; sent_at_ = now();
    }
    if (!call_.response_received()) {
      if (elapsed(sent_at_) <= server_timeout_sec_) return BT::NodeStatus::RUNNING;
      call_.cancel();                                          // 迟到的接受也会被取消，防孤儿 goal
      return unavailable("no response");
    }
    // 结果：SUCCEEDED → 写输出端口；CANCELED → FAILURE；ABORTED → 按 required 策略
  }
  void onHalted() override { call_.cancel(); }                 // 必须实现
};
```

- 回调里只持 client 的 `weak_ptr`，状态放在带 mutex 的共享 `State` 结构里。
- `unavailable()` 按 `required` 端口决定：必需 → FAILURE；可选（如播报）→ 告警后 SUCCESS。
- 端口示例（分段执行）：

```cpp
BT::InputPort<HpaSessionHandlePtr>("session"),
BT::InputPort<std::string>("server_name", "/hpa_executor/execute_segment", ""),
BT::InputPort<double>("timeout_sec", 180.0, "Segment budget; controller obstacle-hold time is not counted"),
BT::InputPort<std::string>("obstacle_hold_topic", "/sa_mppi/obstacle_hold", ""),
BT::OutputPort<NavResult>("outcome"), BT::OutputPort<std::string>("phase"), ...
```

- 播报节点 `Speak` 端口：`text, template, params("k=v;k=v"), priority(0..3), interrupt, dedup_key, wait_until_done, required, action_name=/speech/say, server_timeout=1.0`；`AskUser` 输出 `answer, transcript`。

## 4. 结果分类与恢复

- 动作结果先进 `Classify...Outcome` 节点，转成黑板上的分类；后面的 `ReactiveFallback` 按分类路由：
  - `SWITCH_TRANSIENT` → 重试同一操作；
  - 需要终点局部恢复 → 重新裁剪/直接重试一次；
  - 其它 → 更新阻塞记忆 → 重规划（把阻塞的锚点/走廊/区域连同 TTL 45/30/90 s 一起发给规划服务）。
- 外部阻塞（`/is_stuck` 0→1 边沿）在循环里最先检查：取消 → 等停 → 拍照 → VLM → 播报 → 堵了就重规划，否则清标记。
- 有界：`nav_retry_max=3`，`BlockedRouteMemory.exhausted_for()`。
- 暂停门 `ButtonPauseGate`：底盘按键按住=暂停；松开且轮子静止 ≥1 s 自动恢复；**操作员发的暂停永不自动恢复**。

## 5. 纯逻辑与 ROS 分离

能纯算的逻辑（到达判定、避障停留计时、事件状态机、暂停门、路径工具）写成不依赖 ROS 的类，节点只是薄壳。这样 gtest 不需要 rclcpp，回放/离线复现也能直接调用。
