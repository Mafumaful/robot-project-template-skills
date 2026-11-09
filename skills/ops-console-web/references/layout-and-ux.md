# 页面布局与交互约定

## 1. 页面分工（一个端口，多页面）

| 路由 | 用途 | 说明 |
|---|---|---|
| `/` | 主控制台 | 三个标签：导航测试 / 底层·版本 / 参数调优 |
| `/legacy` | 旧版布局 | 旧系统（flask_ros, Bootstrap）的界面搬到新 API 上，给习惯旧界面的人；`?ui=legacy` 或 localStorage `cdm-ui` 记住选择 |
| `/debug` | 模块调试 | 左侧 220px 模块列表（带状态圆点）+ 右侧模块页；每个模块配测试手册 |
| `/replay` | 数据包回放 | 运行卡片列表、速度链图表、地图回放、行为树窗口 |

新项目照此拆：**主操作页**（日常用）、**调试页**（逐个硬件/模块验证）、**回放页**（事后分析证据），旧界面兼容页按需。

## 2. 主页骨架

```
<header>  品牌(logo+名称+一行副标题“定点 · 路径 · 速度链 · 自动录包 · 版本证据”)
          硬件状态条：有线/无线/温度/麦克风/音响/手柄/雷达/底盘/电量 芯片 | ROS Domain ID 按钮 | CPU/内存条 + 各模块 CPU 柱
          右侧动作：连接 badge · 自动限速滑块 · Ⅱ暂停 ▶恢复 ■停止任务 ■软件急停<kbd>Esc</kbd> · 登录 · 旧版界面 · 主题
<div#banner>  安全提示（可 × 关闭；急停时变红且不可关）
<nav>     胶囊标签 + 分隔 + 外部页面链接(↗) + 右侧当前配置方案名
<main.tab#test>
   <aside#test-side>  图标导轨标签：📍位置 / 🧭路线 / 🌳行为树 / 🗺️建图；« 折叠（[ 键），状态存 localStorage
   <article>  地图面板：工具栏 + 地图种类下拉 + 3D 切换；统计芯片行；工具条；定位状态框；
              canvas（缩放导轨、覆盖层：提示/编辑条/测量条/重定位条/定位分数）；图例
              “03 · 运行证据”：诊断框、信号网格、日志 <details>
<main.tab#lowlayer>  统计条 + 两列：底层节点(分层 驱动/感知/导航/可选，“按顺序拉起”)、版本(+OTA)、定位与模式、硬件、生产点位
<main.tab#params>    左：当前生效 + 方案库(标签筛选)；右：编辑器、完整参数表、警告、应用、应用历史
body 级：浮动摇杆面板 + 🎮 召回按钮、<dialog> 改 Domain、<dialog> 登录、#toast
```

段落标题用编号（“03 · 运行证据”）表达阅读顺序。侧栏每页的流程写在 `title` 里，例如建图：“检查 → 停定位 → 建图 → 保存 → 停止 → 回原地图或切到新地图”。

## 3. 前端实现细节

- 每个文件是 IIFE 或共享少量全局（`$ api node toast state`）；跨文件挂钩用 `window.CdmOta.mount(root, {...})`、`window.onSpeechShow` 这类显式接口。
- DOM 用 `replaceChildren()/append()` 构建；`innerHTML` 只在必须时用，并经过 `esc()` 转义。
- 2D 地图：`<canvas>` + DPR 感知的 `sizeCanvas()`；世界↔像素变换要包含地图原点的 yaw；指针事件：拖动平移、Shift 框选放大；选点/测量/路线/重定位几种模式**互斥**，各有 `setX(on)`；log2 缩放滑块；“好看”步长的网格和比例尺。
- 3D：`vendor/three.min.js` r128 + OrbitControls；后端给 gzip 的 int16 厘米点云，元数据放响应头（`X-Bounds/X-Age/X-Note/X-Frame`）。
- 图表：手绘 canvas，不引图表库。
- 行为树视图：`<ul>` 树 + 折叠箭头 + 节点类型图标 `◆◇▶?▣` + 每节点状态胶囊（running/success/failure/skipped）。

## 4. 与后端通信

- `GET /api/<thing>` 读；`POST /api/<thing>/<verb>` 写，JSON body。
- 写请求带 `X-Nav-Test-Token`（服务端把 `<meta name="nav-token" content="__TOKEN__">` 替换成真 token）+ 同源 Origin 检查 + 需要时 cookie 会话。
- `api()` 失败抛 `Error(result.error || statusText)`，调用处 `toast(e.message, true)`。
- 登录门：`lowlayer.js` 包一层 `window.fetch`，任何 POST 收到 `401 {login:true}` 就弹登录框（不自动重试，让人再点一次）。
- 页面实例身份：`owner = crypto.randomUUID()`；只能暂停/恢复自己发起的作业。

### 轮询节奏（参考值）

| 数据 | 周期 |
|---|---|
| `/api/state` | 500 ms（`setTimeout` 自我调度，不用 setInterval 叠加） |
| 心跳（仅持有作业时） | 700 ms |
| 地图 | 1.5 s |
| 节点 / 单元 | 1 s / 2 s |
| 版本 | 忙 2 s / 闲 10 s |
| 重定位作业 | 1 s（运行中） |
| 导出进度 | 500 ms |
| 调试页 TOF | 100 ms（隐藏时暂停） |

## 5. 交互原则（每条都是现场踩出来的）

1. **一切操作带进度条**。POST 自动顶部条（>150 ms 才显示，`LABEL` 表把路径映射成中文“正在保存地图…”）；后台作业 `progress.job(host, job)` 显示“第 k/n 步 · 步骤名”；进度动画相位用 `animationDelay=-(Date.now()%1200)ms`，重绘不跳。
2. **危险操作双重确认**：前端多行 `confirm()` 写“会做什么 / 前提 / 不会做什么”，后端 `confirm is not True → 400`。
3. **默认只读**：`--demo` 模式所有控制返回 `409 只读 UI 展示模式`；旧系统占用硬件时进入“观察模式”，控件禁用并写明原因。
4. **可关闭的提示**：banner、定位提示、切图提示都有 ×，关闭状态按浏览器记住（`cdm.*Dismissed`），但紧急状态不可关。
5. **测试手册**：调试模块右上 `?`（或按 `h`）打开浮动手册：目的 → 前提 → 步骤（操作→预期，可勾选）→ 常见问题（现象/原因/检查）。手册数据放 `debug-manuals.js`，严格 JSON，测试会解析它。
6. **诚实状态**：`—` 表示未知，“旧值/旧状态”表示过期；定位无效机器人画灰；每个状态给 `hint`（下一步该做什么）。
7. **死人开关**：心跳；`visibilitychange/pagehide` → `sendBeacon('/api/stop')`；摇杆松手、页面隐藏清空按键。
8. **解释副作用**：控件 `title` 写清楚，例如“仅暂停本页面的任务，保留任务并继续录包”“停止外层任务、取消运动并结束录制；不可恢复”。
9. **每次运行留证据**：自动录包、版本证据、日志、可导出（bag+logs .tar），回放页可复制运行 ID。
10. **快捷键**：`Esc` 急停、空格暂停、`[` 侧栏、`J` 摇杆、`?`/`h` 手册、Backspace 撤销、Enter 提交，都在界面上用 `<kbd>` 提示。
