# 视觉风格指南

来源：`tools/nav_test_web/static/style.css` 第一行注释——风格跟随个人站 mafumaful.github.io（Fuwari 主题）：**hue-250 蓝紫色 token、淡色底上的白色圆角卡片、胶囊按钮**。完整源文件在 `../assets/static/style.css`。

## 1. Token（直接复制）

颜色全部用 **oklch**，只靠改 `--hue` 就能换品牌色；状态色固定 hue（绿 160、红 20、琥珀 70）。

```css
:root{
  color-scheme:light;--hue:250;
  --mod-l:.62;--mod-bg:.965;--mod-line:.89;--mod-ink:.46;
  --page-bg:oklch(.95 .01 var(--hue));--card:#fff;--card-2:oklch(.975 .006 var(--hue));
  --line:oklch(.91 .012 var(--hue));--line-strong:oklch(.85 .02 var(--hue));
  --text:oklch(.25 .02 var(--hue));--text-2:oklch(.4 .02 var(--hue));--muted:oklch(.55 .015 var(--hue));
  --primary:oklch(.7 .14 var(--hue));--primary-ink:oklch(.55 .12 var(--hue));--primary-strong:oklch(.6 .15 var(--hue));
  --btn-bg:oklch(.95 .025 var(--hue));--btn-hover:oklch(.9 .05 var(--hue));--btn-active:oklch(.85 .08 var(--hue));
  --plain-hover:oklch(.95 .025 var(--hue));
  --green:oklch(.62 .14 160);--green-bg:oklch(.95 .04 160);
  --red:oklch(.6 .2 20);--red-strong:oklch(.56 .21 20);--red-bg:oklch(.95 .03 20);
  --amber:oklch(.6 .13 70);--amber-bg:oklch(.96 .05 85);--amber-ink:oklch(.45 .1 65);
  --code-bg:oklch(.97 .008 var(--hue));--map-bg:oklch(.93 .01 var(--hue));
  --shadow:0 1px 2px oklch(.4 .03 var(--hue)/.06),0 4px 14px oklch(.4 .03 var(--hue)/.06);
  --shadow-hover:0 2px 4px oklch(.4 .03 var(--hue)/.08),0 10px 28px oklch(.4 .05 var(--hue)/.14);
  --radius:16px;--radius-sm:10px;--ease:cubic-bezier(.2,.8,.2,1);
}
:root[data-theme=dark]{
  color-scheme:dark;
  --mod-l:.74;--mod-bg:.27;--mod-line:.36;--mod-ink:.86;
  --page-bg:oklch(.16 .014 var(--hue));--card:oklch(.23 .015 var(--hue));--card-2:oklch(.2 .015 var(--hue));
  --line:oklch(.3 .015 var(--hue));--line-strong:oklch(.38 .02 var(--hue));
  --text:oklch(.92 .01 var(--hue));--text-2:oklch(.8 .015 var(--hue));--muted:oklch(.66 .015 var(--hue));
  --primary:oklch(.75 .14 var(--hue));--primary-ink:oklch(.78 .12 var(--hue));--primary-strong:oklch(.62 .14 var(--hue));
  --btn-bg:oklch(.33 .035 var(--hue));--btn-hover:oklch(.38 .05 var(--hue));--btn-active:oklch(.43 .07 var(--hue));
  --plain-hover:oklch(.3 .03 var(--hue));
  --green:oklch(.78 .13 160);--green-bg:oklch(.3 .05 160);
  --red:oklch(.7 .17 20);--red-strong:oklch(.58 .19 20);--red-bg:oklch(.3 .07 20);
  --amber:oklch(.8 .12 75);--amber-bg:oklch(.3 .05 75);--amber-ink:oklch(.85 .1 80);
  --code-bg:oklch(.19 .014 var(--hue));--map-bg:oklch(.14 .012 var(--hue));
  --shadow:0 1px 2px #0003,0 4px 14px #0002;--shadow-hover:0 2px 6px #0004,0 12px 30px #0005;
}
```

`--mod-*` 用于“每个模块一个色相”的小芯片：`background:oklch(var(--mod-bg) .03 var(--mh))`，`--mh` 由 JS 按模块设置。

## 2. 主题切换

`<head>` 里同步执行，避免闪烁：

```html
<script>try{const t=localStorage.getItem("nav-theme")||(matchMedia("(prefers-color-scheme: dark)").matches?"dark":"light");document.documentElement.dataset.theme=t;}catch(e){}</script>
```

按钮 `#theme-toggle.icon-btn` 内放太阳 `<g class="icon-sun">` 和月亮 `<path class="icon-moon">` 两个 SVG，CSS 按 `data-theme` 切显示；点击时翻转 `dataset.theme` 并写 localStorage。所有页面共用同一段脚本。（例外：`replay.html` 历史上有一套独立的十六进制 token 和 `prefers-color-scheme` 媒体查询——新项目别学这个，统一用上面的 token。）

## 3. 字体与排版

- `body{font:14px/1.6 system-ui,-apple-system,"PingFang SC","Noto Sans CJK SC","Microsoft YaHei",sans-serif}`
- 等宽：`ui-monospace,"SF Mono",Menlo,monospace`，用于坐标、日志、路径、参数表；数字读数加 `font-variant-numeric:tabular-nums`。
- `h2` 16px/700，前面一根 **4×16px 圆角主色竖条**（`h2::before`）——这是本风格的标志。`h3` 14px/650；`label` 12px/550 `--text-2`；`small/.muted` 12px。

## 4. 版面

- `main{padding:16px 24px 32px;max-width:1880px}`；页面底色 `--page-bg`。
- 卡片：`section{background:var(--card);border-radius:var(--radius);padding:18px 20px;margin-bottom:16px;box-shadow:var(--shadow)}`，hover 换 `--shadow-hover`。
- 圆角层级：卡片 16px → 控件 10px → 卡片内行 12px → 小芯片 6–8px → 胶囊 999px。
- 动效：`transition:.15s var(--ease)`；`button:active{transform:scale(.96)}`；切换标签 `fade-up .35s`；`@media (prefers-reduced-motion)` 关掉所有动画。

## 5. 组件配方

| 组件 | 要点 |
|---|---|
| 按钮（默认） | `background:var(--btn-bg);color:var(--primary-ink);font-weight:600;padding:8px 13px;border-radius:var(--radius-sm)` |
| `.primary` | `--primary-strong` 底、白字 |
| `.danger`（急停） | `--red-strong` 底、16px/750、`min-width:190px`、外圈红晕 `box-shadow:0 0 0 4px oklch(.6 .2 20/.15)` |
| `.stop` | 琥珀底琥珀字（“停止任务”，比急停低一级） |
| `.icon-btn` | 38px 方形透明 |
| 输入框/下拉 | `--card-2` 底、`--line` 边；聚焦环 `0 0 0 3px oklch(.7 .14 var(--hue)/.2)`；下拉箭头用两道 gradient 画 |
| Header | `position:sticky`；`backdrop-filter:blur(14px) saturate(1.4)`；`background:color-mix(in oklab,var(--card) 86%,transparent)`；品牌 logo 38px 圆角方块，hover 旋转 |
| 硬件状态芯片 `.hw-chip` | 22px 高、11.5px 粗体；`.on` 绿底、`.off` 红底；CPU/内存芯片带 34×6px 分段条，模块柱状图用 `data-tip` + `::after` 做提示 |
| 连接 badge | 胶囊 + 前置圆点；`.ok` 绿色带 `pulse` 动画，`.off` 红，默认琥珀“连接中” |
| Banner | 琥珀底 + 圆形“!”；右侧圆形 ×（`.banner-x`）；`.emergency` 变红底白字并有 `alarm` 脉冲阴影，且隐藏 ×（急停提示不可关） |
| Nav 标签 | 白卡片里一排胶囊按钮，`.selected` = `--primary-strong` 白字 + 光晕；新标签页链接带 `↗` |
| 状态胶囊 | 圆点+文字：`st-ready/st-manual` 绿、`st-starting` 琥珀闪、`st-degraded/st-failed/st-stop` 红、`st-auto` 主色 |
| 状态框 | `.diagnosis` 左侧 4px 主色边；`.loc-status` 三态（默认琥珀 / `.ok` 绿 / `.bad` 红 / `.busy` 紫 oklch hue 300） |
| 列表卡 | `--card-2` 底，hover 上浮 2px 或右移 3px；当前项 `box-shadow:inset 4px 0 0 var(--green)` |
| 表格 | sticky 表头、斑马纹；`tr.diff` 旧值红色删除线；`tr.bad` 琥珀底 |
| Toast | 固定底部居中卡片，左 4px 主色边；`.error` 红边 + `--red-bg`；6 s 自动消失 |
| 浮动窗 | 摇杆面板（右下 300px，可拖、`–` 折叠、`×` 隐藏、🎮 悬浮按钮召回）、测试手册（`resize:both`）、导出进度卡（左下） |
| 进度条 | `progress.js` 自带样式：8px 胶囊轨道、斜纹动画填充、`.ok` 绿 `.bad` 红、不确定态左右滑动；页面顶部 3px 请求条 + 标签胶囊 |
| 电池图标 | CSS 画，带光泽动画，`.low` 红色闪烁 |

## 6. 画布配色（地图类页面）

固定语义色，跨页面保持一致：实际轨迹 `#12b987`（绿）、规划路径 `#e8a426`（琥珀）、预览 `#4d82f9`（蓝）、机器人 `#ef4164`、定位丢失 `#9aa3b2`（灰）、正在选点/测量/编辑 `#f97316`（橙）、选中点 `#277fff`、重定位提示 `#a855f7`、代价地图渐变 `#48aef4→#ffc950`。

## 7. 响应式断点

- `≤2400px` 硬件条单独换行；`≥1600px` 画布加高；
- `≤1100px` 底层页单列；`≤1000px` 侧栏变两列网格、取消 sticky；
- `≤600px` 单列，隐藏 header 小字/badge/方案名，画布 380px 高，急停按钮缩小。
