---
name: robot-dev-workflow
description: 机器人/硬件项目的工程流程范本（来自 robot）：worktree 分支、Conventional Commit、PR 模板、部署到真机（dry-run → 备份 → 同步 → 只编译该包 → 原子替换 → 校验 → 回滚脚本）、静态部署与重启的区分、分层测试（Mac 契约测试 → Docker colcon → Gazebo 无头验收 → 离线仿真 → 真机观察）、文档与事故记录格式、“源码已同步/服务已部署/实机已验证”三态。凡是在新项目里开分支、提交、发 PR、部署到设备/机器人、写测试计划、写排障文档、记录事故、回答“这个上线了吗/验证了吗”时都要用这个技能。
---

# 工程流程范本

## 1. 分支与工作区

- 每个功能一个 worktree：`.claude/worktrees/<name>`（`.claude/` 在 gitignore 里）。主工作区留给同事和未提交补丁，**代码只在 worktree 里改**。
- 分支名：`feature/<kebab>`、`fix/<kebab>`、`integration/<name>`（汇总多个功能）、`colleague/<topic>`（原样保留同事分支）、`deploy/<tag>`。
- 汇总分支会过期：合并完就以 `main` 为准，记一笔“integration/xxx 已过期”。

## 2. 提交

- Conventional Commit，英文、小写、祈使句、带 scope：`feat(console): ...`、`fix(hpa): ...`、`docs(deploy): ...`、`test(gazebo): ...`、`perf/chore/build/sync`。
- 标题可以嵌中文界面名：`feat(console): move 从失败点继续 into the 路线 panel's 执行 section`。
- 正文 1–3 段，写**为什么**和具体机制/数字（`vx_max 0.5 -> 1.0`、`stopping distance 1.0 m < goal_threshold 1.4`）。
- 从上游同步的提交：`feat(<pkg>): take <pkg> from release <sha>`，每包一个提交。
- 小而单一；结尾加 Co-Authored-By 行（按当前助手要求）。

## 3. PR

模板（中文为主）：

```markdown
## 问题
（现象、时间、证据：日志行/截图/运行 ID）
## 改动
（按文件/模块列点）
## 测试
- 控制台单元测试 682 个全部通过（/usr/bin/python3 -m unittest discover -s tests）
- 浏览器检查：明/暗主题截图，无 JS 异常，无运动类 POST
## 验证
- 已部署 robot 2026-11-09 10:19（静态替换，未重启）
- 未验证：真机按键实测
```

- 叠加 PR 写明：“Stacked on #53; merge #53 first”。
- **推送和合并前先问用户**。

## 4. 部署到设备（生产机默认只读）

完整流程与命令见 `references/deploy.md`。骨架：

1. **前置守卫**：控制模式 `manual`、任务 `IDLE`、没有 colcon 在跑，否则脚本中止；重启驱动/导航必须有人在机器旁。查询地址用新项目自己的（范本里的 :5000/:8765 只是例子）。
2. **dry-run** 并把文件列表给用户确认（`rsync -rcn --itemize-changes`，排除 build/install/log/__pycache__/.git/.worktrees/*.pyc，不用 `--delete`）。
3. **备份**：`~/pkg_bak/<YYYYmmdd_HHMMSS>-<name>/`；安装级部署另写 `sha256_before.txt / sha256_after.txt / ROLLBACK.sh`。
4. **同步 → 只编译这个包**（`colcon build --merge-install --packages-select <pkg>`）。
5. **原子替换**：写 `<file>.tmp` 再 `mv -f`。
6. **校验真正加载的东西**：`sha256sum`、`/proc/<pid>/maps` 里的 `.so` 路径、`nm -D` 新符号、`ros2 node list | sort | uniq -c`（每个名字只出现一次）、关键 action server 只有一个。
7. **记录**：备份路径、部署时间、提交、是否重启、回滚命令。

- 只改前端静态文件 → 只替换静态文件、不重启；改后端/ROS → 重启对应单元。
- 设备上的文件比仓库新时，用 `patch --dry-run` 打增量 hunk，**按路径比较而不是按提交号**。
- 远程 ROS 命令一律 `timeout` 包裹；`ssh -o BatchMode=yes ... </dev/null`；`pkill -f "[h]pa_planner"` 防止匹配到自己。

## 5. 测试分层（离线优先，从零风险到有风险）

| 层 | 在哪跑 | 内容 |
|---|---|---|
| 契约测试 | Mac，纯 Python | 解析 launch/compose/脚本，钉死参数默认值；docstring 写防的是哪次事故 |
| 控制台单元 | Mac `/usr/bin/python3` | 标准库 unittest + FakeRunner |
| colcon build/test | 构建服务器 Docker | **拷贝源码进去，不挂载工作区**：`git archive HEAD <dirs> \| ssh host "tar -x -C $P"`；lint 单独报告；记录基线（“438 个测试，28 个失败全在同事的包”） |
| 仿真验收 | Gazebo 无头 compose，独立 `ROS_DOMAIN_ID` | 脚本化验收，固定通过行 |
| 离线仿真 | 容器 + rosbag | 参数矩阵 A/B（如 255 次运行） |
| 真机观察 | 只读脚本 | `curl GET`，退出码 0/1/2/3 有文档 |
| 真机运动 | 有人、手按急停、首个目标 2–3 m | 最后一步 |

- 计划里明确 TDD：写失败测试 → 实现 → 全部通过 → 提交。
- 诊断先上**只加日志的补丁**，再改一层，再跑同一路线复现对比。

## 6. 三态，永远分开说

**源码已同步 / 服务已部署 / 实机已验证**。合并、推送、部署、硬件验收分开记录。没测到的写“未验证”，单次运行写“各一次运行，还不是统计结论”。依赖装好 ≠ 功能验证。部署了但没人按过的按钮记“live unproven”。

## 7. 文档

格式与模板见 `references/docs-templates.md`：计划/排障文档（日期·分支·状态头、假设 H1…、分阶段带通过标准和中止条件、“需要你决定的事”）、事故记录（截图命名 `<kind>_YYYYMMDD_HHMMSS.png`、现象/机理/结果/局限）、spec 与 plan（`docs/superpowers/specs|plans/YYYY-MM-DD-<topic>.md`）、部署记录（改了什么/设备上的变化/已验证·未验证/只读核对命令/只回滚这一步）、开发历史（北京时间、需求、分支/提交、部署目录、测试范围、未闭环事项）、回顾视频脚本。

## 8. 项目技能（skills/）的写法

- frontmatter `name` + 一句 `description: Use when …`。
- H1 → 角色/位置表 → 一句工作流 → 有暂停时加带日期的 **Status** 引用块。
- 编号步骤 + 可复制的 bash，每步写验证方式（“Verify: `ros2 topic hz /livox/lidar` 约 10 Hz”）。
- 小节：Gotchas、Known gaps（日期）、Common mistakes | Instead 表、合并必须保留的契约、“Last synced”表（每次同步同一提交更新）。
- 语气：简洁、具体日期和观测值、危险项加粗、诚实标注“未测试 / 只读过源码”。
- 新项目要为这些反复出现的操作各写一个项目技能：SSH 访问、底层启动、部署一个包、与上游/发布仓库同步、仿真验收。

## 9. 与上游发布仓库同步

只从发布仓库流向开发仓库。每个包归类：**mirror**（`rsync -a --delete` 后 `diff -rq` 为空，一包一提交）、**merge**（手工移植 hunk）、**dev-only / release-only**（永不复制）。用只读的 blob-less sparse clone 并禁用 push（`git remote set-url --push origin no_push_readonly`）；用树哈希判断是否一致：`git rev-parse origin/main:<path>` 对比 `HEAD:<path>`。

## 10. 安全底线

- 生产设备默认只读；每次真实同步/编译/重启前 dry-run 并取得用户确认。
- 不硬编码 IP/密钥（用 SSH 别名；密钥泄露就轮换，不抄进笔记）。
- 已知危险操作写成 Hazard 并加粗（例如“地图名必须是 map”“`pkill -f xxx` 会杀掉新框架”）。
- 机器人编辑要回流仓库：“在开发仓库改，不在机器人上改”。
