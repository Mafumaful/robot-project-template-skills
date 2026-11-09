---
name: ops-console-web
description: 机器人/设备运维控制台（Web 操作界面）的范本：视觉风格（oklch hue-250 卡片风、明暗主题）、页面布局、组件、前端轮询架构、Python 标准库后端、进度条/登录/急停/测试手册等交互约定。凡是要给机器人、嵌入式设备、ROS 系统、实验平台、内部工具做网页控制台、调试页、回放页、参数页、监控面板，或要“按 Cyber Guide Dog 8090 控制台的风格”做界面时，都要先用这个技能——即使用户只说“做个网页看状态”“加个按钮”“做个调试页面”。
---

# 运维控制台范本（来自 Cyber Guide Dog 的 8090 CDM 控制台）

源项目：`Cyber_Guide_Dog/tools/nav_test_web/`（约 3 万行，Python 标准库 HTTP 服务 + 原生 JS/CSS，中文界面），跑在机器人上，端口 8090，取代了旧的 flask_ros。新项目的控制台照这个做：**同一套视觉 token、同一种文件组织、同一套安全与进度约定**。

总原则一句话：**控制台是现场操作员的仪器，不是营销页面**——信息密、状态诚实、每个动作看得见进度、危险动作要确认、急停永远可用。

## 何时读哪个文件

| 要做的事 | 读 |
|---|---|
| 配色、字体、组件 CSS、主题切换 | `references/style-guide.md`，起步直接复制 `assets/static/style.css` 的 token 块 |
| 页面布局、各页面分工、交互细节 | `references/layout-and-ux.md` |
| 后端路由、作业(job)模型、ROS 桥、鉴权、systemd、测试 | `references/backend.md` |
| 可直接复用的代码 | `assets/`（见下表） |

`assets/` 是从源项目拷来的可用文件，复制到新项目后按需改名：

| 文件 | 作用 |
|---|---|
| `static/style.css` | 全量样式（前 33 行是 token 块，后面是组件配方）。新项目保留 token 和通用组件，删掉项目专属类 |
| `static/progress.js` | 进度条库：`progress.set/job/clear/track` + 自动给每个同源 POST 显示顶部 3px 请求条 |
| `static/debug-manual.js` | 浮动“测试手册”窗口（可拖、可缩、勾选步骤记在 localStorage） |
| `backend/progress.py` | 作业步骤进度 `plan(job, steps)` / `reach(job, step)`（单调、不回退） |
| `backend/runner.py` | 唯一的外部命令/`/proc` 访问点，可注入，测试时换成 FakeRunner |
| `backend/auth.py` | PBKDF2 密码登录 + HttpOnly cookie + 页面 token；`OPEN_POSTS` 列出永不需要登录的安全接口。默认密码已改为读 `CONSOLE_DEFAULT_PASSWORD`，每个项目自己设 |
| `backend/fakes.py` | 测试替身 `Clock`、`FakeRunner.on(prefix, response)` |

## 十条必须守的约定

1. **技术栈克制**：后端 Python 标准库 `ThreadingHTTPServer`，前端原生 ES2020（`'use strict'`、`const $ = id => document.getElementById(id)`、`node(tag,text,cls)` 建 DOM），不引框架、不引构建工具；第三方库只放 `static/vendor/`（如 three.js r128）。原因：要能直接拷到机器人上跑、没有 npm、出问题能当场读源码。
2. **一个功能一个文件**：后端 `mapping.py / replay.py / ota.py …`，前端 `mapping.js / ota.js …`；文件顶部 docstring 写它做什么并引用设计文档章节（“spec §3”）。
3. **状态从服务器派生**：一个 `/api/state` 每 500 ms 轮询，`renderState(s)` 把所有控件的文字和 disabled 从它算出来；前端不缓存“真相”，localStorage 只放界面偏好（主题、侧栏折叠、已关闭的提示）。
4. **轮询，不用 WebSocket/SSE**；忙时快、闲时慢（例：版本 2 s 忙/10 s 闲）；调试页在 `document.hidden` 时暂停轮询。
5. **每个操作都有进度条**（操作员 2026-10-27 的明确要求）：POST 自动出顶部请求条；后台作业出“第 k/n 步 · 步骤名”条；下载出字节条。
6. **长操作 = 后台作业**：POST 立即回 `202 + job`，前端轮询状态；作业有 `steps`，失败自动回滚并有自己的回滚步骤；结果追加到 `<data>/log/<name>_history.jsonl`。
7. **默认只读，控制要登录，急停永不设门槛**：`/api/estop`、`/api/stop`、心跳、松开摇杆、暂停永远开放；危险操作前端 `confirm()` 写清会做什么/不会做什么，后端还要求 body 里 `confirm: true`。
8. **死人开关**：页面持有作业时每 700 ms 发心跳；页面隐藏/关闭用 `navigator.sendBeacon('/api/stop')` 让机器人停下；摇杆松手 300 ms 内停。
9. **诚实显示**：未知值显示 `—`，过期值标“旧值”，缺失值不伪造成 0；定位无效时机器人箭头变灰并给出原因代码和下一步提示；状态文字说清“是什么、不是什么、该怎么办”。
10. **调试模块必须带测试手册**：每个 /debug 模块配一份浮动“测试手册”（目的 / 前提 / 操作→预期 勾选步骤 / 常见问题 现象-原因-检查），有单元测试强制每个模块都有手册。

## 新建一个控制台的步骤

1. 建目录：`server.py`、`core.py`（`atomic_json`、路径越界检查 `inside()`）、`runner.py`、`auth.py`、`progress.py`、`static/{index.html,style.css,app.js,progress.js}`、`tests/{fakes.py,test_*.py}`、`README.md`、`VERIFICATION.md`、`systemd/`、`deploy/`。
2. 复制 `assets/static/style.css` 的 `:root` 与 `:root[data-theme=dark]` token 块和通用组件（section 卡片、按钮、badge、banner、toast、进度条），`index.html` 头部放主题初始化脚本（见 style-guide）。
3. 页面骨架：粘性毛玻璃 header（品牌 + 硬件状态条 + 连接 badge + 急停 + 登录 + 主题切换）→ banner → nav 胶囊标签 → `main.tab`（左侧可折叠 aside + 右侧 article）。
4. 后端按 `references/backend.md` 的 Handler 链：每个功能模块提供 `handle_get(handler, path)` / `handle_post(...)`，返回 True 表示已处理。
5. 每个功能先写失败的单元测试（`FakeRunner` 注入），再实现；浏览器检查脚本断言“没有 JS 异常、没有发出任何运动类 POST”。
6. README 写：部署方法、现场检查、接口列表、边界（“不是/不会”）、测试数；VERIFICATION 写：验证了什么、**没有**证明什么。

## 文案风格（中文界面）

- 短名词标签，`·` 作分隔（`定点 · 路径 · 速度链`），界面名用「」，全角冒号“：”，链路用 `→`，新标签页链接加 `↗`。
- 几乎每个控件都有 `title=` 说明副作用。
- 按钮可以带 emoji 图标（📍 🧭 🗺️ 🎯 💾 🎮 ⚠ ✅），header/nav 用 24×24 线性 SVG 图标（lucide 风格，stroke-width 2）。
- 快捷键在界面上用 `<kbd>` 标出：`Esc` 急停、`[` 折叠侧栏、`J` 摇杆、`?`/`h` 手册、空格暂停。
- 错误统一 `{"error": "中文原因"}`，前端 toast 6 秒，红色左边框。

## 常见错误

| 错误 | 改为 |
|---|---|
| 引入 React/Vue/Tailwind 构建链 | 原生 JS + 一份 CSS token |
| 用十六进制随手配色 | 只用 token；状态色只用 green/amber/red 三档 |
| 长操作同步阻塞 HTTP 请求 | 202 + 后台作业 + 轮询 + 步骤进度条 |
| 急停/停止也要求登录 | 放进 `OPEN_POSTS` |
| 隐藏标签页继续让机器人跑 | `visibilitychange` → `sendBeacon('/api/stop')` |
| 显示 0 代替“没有数据” | 显示 `—` 或“旧值”，并说明原因 |
| 运行时 `destroy_subscription` 常驻订阅 | 只对请求级临时对象用 destroy，常驻对象在关闭时统一清理 |
