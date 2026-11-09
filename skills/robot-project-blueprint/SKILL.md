---
name: robot-project-blueprint
description: 以 Cyber Guide Dog（ROS 2 导盲机器人）为范本的项目总蓝图：设计理念、系统分层与数据流、仓库目录、包与话题命名、参数/launch/systemd 组织、安全与看门狗原则，并索引行为树(robot-bt-framework)、运维网页(ops-console-web)、工程流程(robot-dev-workflow)三个子技能。用户开始任何新项目（尤其机器人、ROS、嵌入式、带硬件或带网页控制台的项目）、说“按之前那个项目的方式做”“参考 Cyber Guide Dog/导盲犬项目”“搭个项目框架”“规划架构”“项目初始化”时，先用这个技能。
---

# 项目总蓝图（范本：Cyber Guide Dog）

用户把 Cyber Guide Dog 定为之后所有项目的范本。新项目开工时：先读本文件定架构和原则，再按需加载子技能。

**继承的是模式，不是数值。** 本技能和子技能里的具体端口（8090/8765/5000）、话题名、DOMAIN_ID、阈值、主机别名（Mini6Test、server5090）、目录（~/cdm_data、~/pkg_bak）都是范本项目的实例。新项目照搬结构和原则，具体值要按新项目重新定，未知的写成 `<待定>` 占位并列进“需要你决定的事”。

| 子技能 | 何时加载 |
|---|---|
| `robot-bt-framework` | 任务编排、行为树、执行器、恢复策略、到达判定 |
| `ops-console-web` | 任何网页界面：控制台、调试页、回放页、参数页 |
| `robot-dev-workflow` | 分支/提交/PR、部署到设备、测试分层、文档与事故记录 |

## 1. 设计理念（十二条）

1. **单一决策者**：行为树拥有所有任务决策（执行、切图、失败处理、播报）；规划器、控制器、语音、视觉只提供能力（action/service），不做“差不多就算了”的判断。
2. **权威契约**：关键结论（到达、成功）只由一个地方裁定，有明确数值阈值和稳定样本；快照/事件有版本化 JSON schema 和校验函数；launch 参数默认值有契约测试钉死。
3. **失败关闭**：拿不准就停。恢复成功不等于任务成功；取消未确认就停任务；策略不一致就 halt；框架掉线 → 取消目标 + 切手动。
4. **处处超时与看门狗**：每个等待有超时；控制链有心跳（自动模式 300 ms 无指令即停）；页面关闭即停；数据过期显示“旧值”。缺的看门狗写进“未闭环事项”。
5. **唯一所有者**：一个话题一个发布者、一个串口一个进程、节点名唯一；检测到双发布者就锁 STOP 直到操作员重新选择。
6. **安全动作永不设门槛**：急停/停止/心跳不要求登录、不经过 demo 门。
7. **默认安全、功能可选**：新功能开关默认关；`optional_include` 跳过缺失包；保留旧执行器以便回滚。
8. **配置驱动、仓库即真理**：参数进包内 yaml（不放 /tmp），点位/地图有唯一真理源文件；安装目录带版本指纹；树和配置的哈希写进快照。
9. **可观测但不可误控**：生产监控只读；可注入/覆盖的只有隔离沙箱；每次运行留证据（录包、版本、日志、可导出）。
10. **优雅降级**：可选能力软导入，缺了就 no-op；播报失败不影响导航。
11. **诚实**：未知是 `—`，过期是“旧值”；源码已同步 / 服务已部署 / 实机已验证 三态分开说。
12. **小步、可回滚**：一个功能一个 worktree、一个 PR；每次部署先备份、旁边就是回滚脚本。

## 2. 系统分层与数据流

```
传感器        Livox MID360 (/livox/lidar,/livox/imu)  TOF 8x8 (/tof_8x8/*)  相机(/input_image/compressed)  超声
  ↓
定位/建图     dlo 定位 (map→base_link)  · faster_lio + pgo 建图  · pointcloud_to_laserscan → /scan  · relocalizer(ICP 门, /initialpose)
  ↓
代价地图      global: static+obstacle+inflation · local: obstacle + tof 层 + inflation
  ↓
规划          HPA 分层路网(/hpa_planner/plan_path) → Nav2 SmacPlannerHybrid
  ↓
控制          SA-MPPI 控制器 → /cmd_vel → velocity_smoother → 摇杆仲裁(joystick_node, 模式 STOP/RUN, 心跳) → /ctl/cmd_vel
  ↓
执行器        car_chassis_node (CAN/RS485) → /wheel_odom, 电量, 超声

任务层        App / 网页控制台 / 语音(小智 NavigateTo) → /mission/start → mission_bt_node(行为树)
                → HPA 分段执行 action → Nav2 /navigate_to_pose
交互层        BT Speak → /speech/say action (预录音 → 云 TTS → 文字日志 的后端链)
              VLM：/analysis_camera Trigger → nav_vlm (通义 qwen-vl) → JSON 判定
运维层        8090 控制台：systemd 节点组管理、版本、建图、重定位、回放、参数、调试
```

新项目照此分层：**驱动 → 感知/定位 → 世界模型 → 规划 → 控制 → 仲裁/安全 → 执行器**，任务层（行为树）和交互层（语音/视觉）只通过 action/service 调能力，运维层（网页）只通过受控接口管理进程和读状态。

## 3. 仓库组织

```
<repo>/
  <Planning_ws>/            主 ROS 2 工作区：bt_planner/ (bt + msgs + task_scheduler)、规划器包、控制器包、语音包、BT 插件包、机体模型包
  <driver_pkgs>/            每个硬件一个包：chassis、tof、camera、雷达驱动（子模块）
  <voice_client>/           语音客户端（非 ROS 也可）
  tools/<console>/          运维网页控制台（见 ops-console-web）
  deploy/<robot>/           部署/回滚脚本、systemd drop-in、分步目录 steps/01..08
  docker/                   仿真(gazebo 无头)、构建镜像、驱动容器
  docker-compose.*.yml      仿真、mock 栈
  scripts/                  参数校验、只读观察脚本、诊断
  test/                     仓库级契约测试（pytest）
  skills/                   项目技能（SSH、底层启动、部署包、上游同步、仿真验收）
  docs/                     计划、排障、spec/plan、事故截图、开发历史、视频脚本
  third_party/              子模块
```

大型第三方（FAST-LIO、navigation2、跟踪库）用 git submodule；开发仓库与发布仓库分离，发布包是编译产物（Cython `.so`），同步规则见 `robot-dev-workflow`。

## 4. 命名约定

- **话题**：绝对路径、按子系统分根命名空间：`/mission/*`、`/navigation_bt/*`、`/hpa_planner/*`、`/hpa_executor/*`、`/speech/{say,ask,stop,events}`、`/sa_mppi/{obstacle_hold,perf_stats}`、`/tof_8x8/{grid,points,obstacle_points,health}`、`/cdm/max_speed{,/set,/adjust}`（运维控制台自有前缀）、`/ctl/cmd_vel`（最终速度）、`/control_mode`。
- 外部可调的值同时给 `set` 和 `adjust` 两个话题（语音“快一点”用 adjust）。
- 用标准消息就用标准（`nav2_msgs/SpeedLimit`），注意语义陷阱（0 = 不限速，停住用 1e-6）。
- **ROS_DOMAIN_ID**：原则是“真机、仿真、每个隔离验收各用一个不同的 ID”，DDS 配置文件入库。（范本项目里是真机 130、仿真 30、验收 87/177——这些只是例子，新项目要自己选号并写进 README，不要照抄。）
- **包内文件**：`config/<pkg>.yaml` 按节点名分节、中文分节注释、参数旁写理由和事故日期；`launch/*.launch.py` 用 `start_xxx` 开关。

## 5. 进程与启动

- systemd 用户单元，按“节点组”切分：`chassis, joystick, tof, lidar, localization(requires lidar), relocalizer, nav@<version>, framework@<version>(requires nav), vision, vlm, voice, mapping(conflicts localization)`。
- 每组一个 `<proj>-<group>.service`（`KillMode=control-group`、`KillSignal=SIGINT`、`TimeoutStopSec=20`、`Restart=no` 让崩溃可见），统一由 `<proj>-stack.target` 拉起；版本化组用模板单元 `@<version>`，开机启哪个版本写在 target 的 drop-in。
- 每组声明就绪检查：`rate`（话题频率）、`fresh`（最近有消息）、`node`、`lifecycle`、`mission`、`http`、`process`。
- 运维控制台自己 `Restart=on-failure`。

## 6. 新项目开工清单

1. 写一页设计 spec（目标、分层图、接口表、安全约束、验证计划），放 `docs/superpowers/specs/`。
2. 按第 3 节建仓库骨架，建 `skills/` 并先写 SSH/启动/部署三个项目技能。
3. 定义话题/服务/action 命名表和 DOMAIN_ID。
4. 先做安全链：速度仲裁 + 心跳 + 急停 + 单一发布者检查。
5. 搭行为树骨架（加载 `robot-bt-framework`）：根树 Stop/Pause/Active/Idle、快照、只读监控。
6. 搭运维控制台（加载 `ops-console-web`）：状态条、急停、节点组、日志、进度条。
7. 测试分层与部署脚本（加载 `robot-dev-workflow`）：契约测试、Docker 构建、仿真验收、备份+回滚。
8. 在项目记忆里记：设备访问方式、部署位置、备份目录、当前已部署提交和验证状态。

## 7. 语言

- 面向操作员的界面、文档、PR 描述：中文。
- 代码、标识符、提交信息、技能（可中英混合）：英文为主。
- 时间一律北京时间、精确到分钟，日期写绝对日期。
