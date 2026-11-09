'use strict';
// Progress bars for every console operation (operator request 2026-10-27: 一切操作都带进度条).
//   progress.set(host, {value, label, tone})  one bar as the last child of host: value 0..1, null = indeterminate,
//                                             tone '' (running) | 'ok' | 'bad'. Re-calling updates the same bar.
//   progress.job(host, job, tone)             a background job's step bar from job.steps / job.step_no (progress.py);
//                                             tone null removes the bar.
//   progress.clear(host)
//   progress.track(promise, label)            the top-of-page bar while promise runs (GETs a person started).
// Every same-origin POST in flight drives the top-of-page bar on its own (heartbeat / teleop frames excluded).
// Styles live here so pages without style.css (回放) get the same bar; colours come from the page's tokens when it has them.
window.progress = (() => {
  const css = document.createElement('style');
  css.textContent = `
.pbar{display:flex;align-items:center;gap:8px;margin:6px 0 2px;min-width:0}
.pbar-track{position:relative;flex:1;min-width:60px;height:8px;border-radius:99px;overflow:hidden;
  background:color-mix(in oklab,var(--primary,#4f78c4) 16%,var(--card-2,#eef1f6))}
.pbar-track>i{position:absolute;inset:0 auto 0 0;width:0;border-radius:inherit;background:var(--primary,#4f78c4);
  transition:width .4s cubic-bezier(.2,.8,.2,1);
  background-image:linear-gradient(45deg,#fff3 25%,#0000 25%,#0000 50%,#fff3 50%,#fff3 75%,#0000 75%);
  background-size:16px 16px;animation:pbar-stripes 1.2s linear infinite}
.pbar.indeterminate .pbar-track>i{width:35%;transition:none;background-image:none;animation:pbar-slide 1.2s ease-in-out infinite}
.pbar.ok .pbar-track>i{background:var(--green,#2e9e6b);animation:none}
.pbar.bad .pbar-track>i{background:var(--red,#d9475b);animation:none}
.pbar-label{flex:0 1 auto;font-size:12px;color:var(--text-2,#555);white-space:nowrap;overflow:hidden;text-overflow:ellipsis;max-width:65%}
.pbar-label:empty{display:none}
@keyframes pbar-stripes{to{background-position:16px 0}}
@keyframes pbar-slide{from{left:-35%}to{left:100%}}
#req-bar{position:fixed;top:0;left:0;right:0;height:3px;z-index:60;pointer-events:none;overflow:hidden;opacity:0;transition:opacity .2s}
#req-bar.on{opacity:1}
#req-bar>i{position:absolute;top:0;bottom:0;width:30%;background:var(--primary,#4f78c4);animation:pbar-slide 1.1s ease-in-out infinite}
#req-label{position:fixed;top:6px;right:12px;z-index:60;pointer-events:none;font-size:12px;padding:3px 10px;border-radius:99px;
  background:var(--card,#fff);color:var(--text-2,#555);box-shadow:0 2px 10px #0002;opacity:0;transition:opacity .2s}
#req-label.on{opacity:1}
@media (prefers-reduced-motion:reduce){.pbar-track>i,#req-bar>i{animation-duration:3s}}`;
  document.head.append(css);

  // A bar re-created on every render (hosts that replaceChildren each poll) starts its animation at the same phase as
  // the one it replaces, so the stripes keep moving instead of jumping back.
  const phase = el => { el.style.animationDelay = `-${Date.now() % 1200}ms`; };
  function set(host, {value = null, label = '', tone = ''} = {}) {
    if (!host) return null;
    let bar = host.querySelector(':scope > .pbar');
    if (!bar) {
      bar = document.createElement('div'); bar.setAttribute('role', 'progressbar');
      const track = document.createElement('div'); track.className = 'pbar-track'; track.append(document.createElement('i'));
      const text = document.createElement('span'); text.className = 'pbar-label';
      bar.append(track, text); host.append(bar);
    }
    const indeterminate = value === null || !Number.isFinite(value), fill = bar.querySelector('i');
    bar.className = 'pbar' + (indeterminate ? ' indeterminate' : '') + (tone ? ' ' + tone : '');
    const pct = indeterminate ? null : Math.round(Math.max(0, Math.min(1, value)) * 100);
    fill.style.width = indeterminate ? '' : `max(${pct}%, 6px)`; phase(fill);
    if (indeterminate) bar.removeAttribute('aria-valuenow'); else bar.setAttribute('aria-valuenow', pct);
    bar.querySelector('.pbar-label').textContent = label; bar.setAttribute('aria-label', label || '进行中');
    return bar;
  }
  function clear(host) { host?.querySelector(':scope > .pbar')?.remove(); }
  // Step i of n: the bar shows the i-1 steps already done, the label names the one in progress.
  function job(host, j, tone = '', nice = s => s) {
    if (!j || tone === null) return clear(host);
    const n = j.steps?.length || 0, k = Math.min(j.step_no || 0, n);
    if (tone) return set(host, {value: 1, tone, label: tone === 'ok' ? `完成${n ? ` · ${n} 步` : ''}` : ''});
    const step = j.step ? nice(j.step) : '';
    if (!n) return set(host, {label: step || '进行中'});
    return set(host, {value: Math.max(0, k - 1) / n, label: `第 ${Math.max(k, 1)}/${n} 步${step ? ' · ' + step : ''}`});
  }

  // ---- top-of-page bar: any same-origin POST, plus GETs passed to track() ----
  const QUIET = ['/api/heartbeat', '/api/teleop', '/api/teleop/release'];
  const LABEL = {
    '/api/start': '开始任务', '/api/stop': '停止任务', '/api/estop': '软件急停', '/api/pause': '暂停任务', '/api/resume': '恢复任务',
    '/api/release': '解除锁存', '/api/record': '录制', '/api/preview': '规划路线', '/api/maps/points': '保存位置',
    '/api/maps/plan': '检查切换计划', '/api/maps/switch': '提交地图切换', '/api/maps/edit': '保存并加载地图',
    '/api/maps/rollback': '回滚地图', '/api/maps/backup/delete': '删除备份', '/api/relocalize/plan': '检查重定位计划',
    '/api/relocalize/start': '提交重定位', '/api/relocalize/confirm': '确认位置', '/api/params/preview': '预览参数',
    '/api/params/save': '保存方案', '/api/params/apply': '提交参数应用', '/api/params/delete': '删除方案', '/api/params/rebase': '更新基线',
    '/api/nodes/start': '启动节点', '/api/nodes/stop': '停止节点', '/api/nodes/chain': '按顺序拉起', '/api/nodes/optional': '保存开机选项',
    '/api/units/restart': '重启单元', '/api/versions/plan': '检查版本切换', '/api/versions/switch': '提交版本切换',
    '/api/mapping/start': '开始建图', '/api/mapping/save': '提交保存地图', '/api/mapping/stop': '停止建图',
    '/api/mapping/finish': '结束建图', '/api/mapping/cancel': '取消建图', '/api/mode/auto': '切到自动', '/api/mode/manual': '切到手动',
    '/api/login': '登录', '/api/logout': '退出登录', '/api/prod-locations/import': '导入点位', '/api/replay/tags': '保存标签',
    '/api/replay/delete': '删除记录', '/switch': '提交 A/B 切换'};
  const running = new Map(); let seq = 0, shown = false, timer = null;
  let top = null, topLabel = null;
  function mount() {
    if (top || !document.body) return;
    top = document.createElement('div'); top.id = 'req-bar'; top.append(document.createElement('i'));
    topLabel = document.createElement('div'); topLabel.id = 'req-label'; topLabel.setAttribute('role', 'status');
    document.body.append(top, topLabel);
  }
  function renderTop() {
    mount(); if (!top) return;
    const labels = [...running.values()], on = labels.length > 0 && shown;
    top.classList.toggle('on', on); topLabel.classList.toggle('on', on);
    if (on) topLabel.textContent = labels[labels.length - 1] + '…' + (labels.length > 1 ? ` 等 ${labels.length} 项` : '');
  }
  function begin(label) {
    const id = ++seq; running.set(id, label);
    // Answers under 150 ms do not flash a bar.
    if (!shown && !timer) timer = setTimeout(() => { timer = null; if (running.size) { shown = true; renderTop(); } }, 150);
    renderTop(); return id;
  }
  function end(id) {
    running.delete(id);
    if (!running.size) { clearTimeout(timer); timer = null; shown = false; }
    renderTop();
  }
  function track(promise, label = '读取中') {
    const id = begin(label);
    return Promise.resolve(promise).finally(() => end(id));
  }
  const nativeFetch = window.fetch.bind(window);
  window.fetch = (input, init) => {
    let label = null;
    try {
      const method = ((init && init.method) || (input && input.method) || 'GET').toUpperCase();
      const url = new URL(typeof input === 'string' ? input : input.url, location.href);
      if (method === 'POST' && !QUIET.includes(url.pathname)) label = LABEL[url.pathname] || '处理中';
    } catch (e) { /* never let the bar break a caller's fetch */ }
    const result = nativeFetch(input, init);
    return label ? track(result, label) : result;
  };
  return {set, clear, job, track};
})();
