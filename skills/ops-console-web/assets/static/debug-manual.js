'use strict';
// 模块调试 · 测试手册: a floating, non-modal window showing the current module's manual (static/debug-manuals.js), so the
// operator can follow the steps while clicking in the module. Same pattern as the replay page's 行为树 window: drag
// by the title bar, CSS resize corner, minimise / close; geometry and the per-module 已完成 ticks live in localStorage.
// The header button or the ? / h key toggles it; switching modules switches the manual. Shares $ with debug.js.
(function () {
  const MANUALS = window.DEBUG_MANUALS || {};
  const WIN_KEY = 'debug-manual-win', DONE_KEY = 'debug-manual-done';
  const store = { get(k) { try { return JSON.parse(localStorage.getItem(k)); } catch (e) { return null; } },
                  set(k, v) { try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) {} } };
  const win = Object.assign({ open: false, min: false }, store.get(WIN_KEY) || {});
  const escText = t => String(t ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
  let mod = null, rendered = null;

  // ---------- content ----------
  const doneSet = () => new Set(((store.get(DONE_KEY) || {})[mod]) || []);
  function saveDone(set) { const all = store.get(DONE_KEY) || {}; all[mod] = [...set]; store.set(DONE_KEY, all); }

  function render() {
    const m = MANUALS[mod], body = $('manbody');
    rendered = mod;
    $('manmod').textContent = m ? m.title : (mod || '');
    if (!m) { body.innerHTML = `<p class="muted">模块「${escText(mod)}」还没有测试手册。</p>`; return; }
    const done = doneSet();
    const steps = m.steps.map((s, i) => `<li class="man-step${done.has(i) ? ' done' : ''}">`
      + `<input type="checkbox" id="man-step-${i}" data-step="${i}"${done.has(i) ? ' checked' : ''} title="已完成">`
      + `<div><label for="man-step-${i}"><b>${i + 1}. ${escText(s.title)}</b></label>`
      + `<div class="act">${escText(s.action)}</div><div class="exp">${escText(s.expect)}</div></div></li>`).join('');
    const faq = m.faq.map(f => `<dt>${escText(f.symptom)}</dt><dd><span class="cause">${escText(f.cause)}</span><br>${escText(f.check)}</dd>`).join('');
    body.innerHTML = `<h3>目的</h3><p>${escText(m.purpose)}</p>`
      + `<h3>前提</h3><ul>${m.preconditions.map(p => `<li>${escText(p)}</li>`).join('')}</ul>`
      + `<h3>步骤 <small>每步：操作 → 预期结果；勾选记为已完成</small></h3><ol class="man-steps">${steps}</ol>`
      + `<h3>常见问题 / 失败表现</h3><dl class="man-faq">${faq}</dl>`
      + `<div class="man-foot"><span id="mancount"></span><button type="button" id="manreset">重置勾选</button></div>`;
    count();
    body.querySelectorAll('[data-step]').forEach(box => box.addEventListener('change', () => {
      const set = doneSet(); box.checked ? set.add(Number(box.dataset.step)) : set.delete(Number(box.dataset.step));
      saveDone(set); box.closest('.man-step').classList.toggle('done', box.checked); count();
    }));
    $('manreset').addEventListener('click', () => { saveDone(new Set()); render(); });
  }
  function count() {
    const m = MANUALS[mod]; if (!m) return;
    $('mancount').textContent = `已完成 ${doneSet().size} / ${m.steps.length}`;
  }

  // ---------- window ----------
  function clamp() {
    const el = $('manpanel'); if (el.hidden || win.x == null) return;
    const r = el.getBoundingClientRect();
    const x = Math.max(0, Math.min(win.x, innerWidth - Math.min(r.width, innerWidth)));
    const y = Math.max(0, Math.min(win.y, innerHeight - Math.min(r.height, innerHeight)));
    el.style.left = x + 'px'; el.style.top = y + 'px'; el.style.right = 'auto';
  }
  function apply() {
    const el = $('manpanel');
    el.hidden = !win.open; el.classList.toggle('min', !!win.min);
    $('manmin').textContent = win.min ? '▢' : '–'; $('manmin').setAttribute('aria-label', win.min ? '还原' : '最小化');
    $('manshow').setAttribute('aria-pressed', String(!!win.open));
    if (win.w) el.style.width = win.w + 'px';
    el.style.height = win.h && !win.min ? win.h + 'px' : '';
    if (win.open && rendered !== mod) render();
    clamp();
  }
  const save = () => store.set(WIN_KEY, win);
  function toggle() { win.open = !win.open; if (win.open) win.min = false; save(); apply(); }

  $('manshow').addEventListener('click', toggle);
  $('manclose').addEventListener('click', () => { win.open = false; save(); apply(); });
  $('manmin').addEventListener('click', () => { win.min = !win.min; save(); apply(); });
  $('manhead').addEventListener('dblclick', e => { if (!e.target.closest('button')) { win.min = !win.min; save(); apply(); } });
  $('manhead').addEventListener('pointerdown', e => {
    if (e.button !== 0 || e.target.closest('button')) return;
    const head = $('manhead'), r = $('manpanel').getBoundingClientRect(), dx = e.clientX - r.left, dy = e.clientY - r.top;
    head.setPointerCapture(e.pointerId);
    const move = ev => { win.x = ev.clientX - dx; win.y = ev.clientY - dy; clamp(); };
    const up = () => { head.removeEventListener('pointermove', move); head.removeEventListener('pointerup', up); save(); };
    head.addEventListener('pointermove', move); head.addEventListener('pointerup', up);
  });
  new ResizeObserver(() => {
    const el = $('manpanel'); if (el.hidden || win.min) return;
    const w = el.offsetWidth, h = el.offsetHeight;
    if (w && h && (w !== win.w || h !== win.h)) { win.w = w; win.h = h; save(); }
  }).observe($('manpanel'));
  window.addEventListener('resize', clamp);
  document.addEventListener('keydown', e => {
    if (e.ctrlKey || e.metaKey || e.altKey || e.target.closest('input,textarea,select')) return;
    if (e.key === '?' || e.key === 'h') { e.preventDefault(); toggle(); }
  });

  // debug.js calls this whenever a module page is shown (also on resize); re-render only when the module changed.
  window.onDebugPageShown = name => { if (name !== mod) { mod = name; if (win.open) render(); } };
  apply();
})();
