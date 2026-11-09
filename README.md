# Robot Project Template Skills

把 Cyber Guide Dog（ROS 2 导盲机器人）整理成的项目范本，打包成 4 个 Claude Code 技能。之后的新项目都按这里的模式来做。

| 技能 | 内容 |
|---|---|
| [`robot-project-blueprint`](skills/robot-project-blueprint/SKILL.md) | 总入口：12 条设计理念、系统分层与数据流、仓库目录、命名约定、systemd 节点组、新项目开工清单；指向下面三个 |
| [`robot-bt-framework`](skills/robot-bt-framework/SKILL.md) | 行为树（BehaviorTree.CPP v3 + ROS 2）：树的组织、Stop/Pause/Active/Idle 根树、黑板约定、节点模板、到达契约、恢复策略、诊断与测试 |
| [`ops-console-web`](skills/ops-console-web/SKILL.md) | 运维网页控制台：oklch hue-250 视觉 token、页面布局、交互约定（进度条、急停免登录、死人开关、测试手册）、Python 标准库后端；附可复制的 CSS/JS/Python 文件 |
| [`robot-dev-workflow`](skills/robot-dev-workflow/SKILL.md) | 工程流程：worktree、提交与 PR、部署到设备（预演→备份→同步→只编译该包→校验→回滚）、测试分层、文档模板、“源码已同步 / 服务已部署 / 实机已验证”三态 |

**继承的是模式，不是数值。** 技能里出现的端口、话题名、DOMAIN_ID、主机别名、目录都是范本项目的实例，新项目要重新定。

## 安装

用户级安装（所有项目都能用）：

```bash
git clone git@github.com:Mafumaful/robot-project-template-skills.git
cp -R robot-project-template-skills/skills/* ~/.claude/skills/
```

只给某个项目用：把 `skills/<name>` 复制到该项目的 `.claude/skills/`。

`dist/` 里是同样内容的 `.skill` 打包文件，可以在 Claude 应用里直接安装。

## 测试结果（2026-11-09）

用 3 个新项目任务（送餐机器人骨架、电机调试页、部署脚本 + PR）做了两轮对比测试：

| 轮次 | 用技能 | 不用技能 |
|---|---|---|
| 第 1 轮 | 92% | 57% |
| 第 2 轮（修复后） | 96% | 49% |

测试后修复：不再照搬范本的具体数值；模板 `auth.py` 改为在创建 `Auth` 时读取 `CONSOLE_DEFAULT_PASSWORD`（原先在导入时读取，导致整套测试依赖执行顺序）。

## 更新

源项目的做法变了（新的控制台模式、BT 契约调整等）时，改 `skills/` 里对应的文件，重新打包 `dist/`，再提交。
