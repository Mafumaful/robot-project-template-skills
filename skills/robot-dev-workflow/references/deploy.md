# 部署流程细节

## 只读快照（每个维护窗口开始前）

```bash
T=~/robot_window/$(date +%Y%m%d_%H%M%S); mkdir -p $T
ps -eo pid,args > $T/ps.txt
timeout 10 ros2 node list | sort > $T/nodes_before.txt
systemctl --user list-units --no-pager > $T/units.txt
readlink -f ~/robot_data/maps/map.yaml > $T/map.txt
```

窗口结束时 diff 同一组信息，差异写进记录。

## 前置守卫（脚本里，失败即退出）

下面的 URL 是范本项目的（旧后端 :5000、BT 监控 :8765）。新项目换成**自己的**“控制模式”和“任务状态”查询方式；还没有就在脚本顶部写成 `<待定>` 变量并提醒用户，不要照抄这两个地址。

```bash
mode=$(curl -s "$CONTROL_MODE_URL")      # 范本：127.0.0.1:5000/api/control/mode，必须 manual
mission=$(curl -s "$MISSION_STATUS_URL") # 范本：127.0.0.1:8765/api/v1/mission/status，必须 IDLE
pgrep -f "[c]olcon build" && { echo "colcon running"; exit 1; }
```

## 包级部署

```bash
EX="--exclude=build --exclude=install --exclude=log --exclude=__pycache__ --exclude=.git --exclude=.worktrees --exclude=*.pyc"
rsync -rcn --itemize-changes $EX <dev>/ Robot:ws/src/<pkg>/          # 1 dry-run，给用户看
B=~/pkg_bak/$(date +%Y%m%d_%H%M%S)-<name>
ssh Robot "mkdir -p $B && cp -a ~/ws/src/<pkg> $B/"                  # 2 备份
rsync -rc $EX <dev>/ Robot:ws/src/<pkg>/                              # 3 同步
ssh Robot 'cd ~/ws && source env.sh && colcon build --merge-install --packages-select <pkg>'  # 4 只编译该包
```

消息包变了要把依赖它的包一起编。

## 安装级部署（替换 install/ 里的文件）

- 部署脚本生成 `~/<ab_backups>/<ts>_before_<name>/`：原文件、`sha256_before.txt`、`sha256_after.txt`、可直接运行的 `ROLLBACK.sh`。
- 每个文件 `cp new file.tmp && mv -f file.tmp file`。
- 校验脚本 `verify_deploy.sh`：哈希、`/proc/<pid>/maps`、`nm -D`、节点唯一、action server 唯一。

## 控制台类服务

- 只改 html/js/css：替换静态文件即可，不重启；若设备上的静态文件有仓库里没有的补丁，先合并再替换（**patch，不要整文件覆盖**）。
- 改 Python：备份 → 替换 → `systemctl --user restart <unit>` → 看 journal → 浏览器检查。
- Nav2 参数：改 `install/share/<pkg>/param/<robot>.yaml` → 专用重启脚本；运行中的节点要到下次重启才生效。

## 回滚

- 每个部署脚本旁边放回滚脚本（`rollback_to_release.sh`、`rollback_to_flask.sh [nodes_before.txt]`）。
- 回滚后 diff 节点列表，确认回到快照状态。

## 远程执行习惯

```bash
ssh Robot 'bash -s' < deploy/<script>.sh
ssh -o BatchMode=yes Robot 'timeout 10 ros2 topic hz /scan' </dev/null
pkill -f "[h]pa_planner.launch.py"     # 方括号避免匹配到 pkill 自己所在的 shell
```

## 记录

部署后在项目记忆/历史文档写一行：功能、提交、部署时间（北京时间）、方式（静态/重启）、备份路径、回滚命令、实机验证状态（已验证 / live unproven）。
