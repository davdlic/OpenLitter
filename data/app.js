/*
 * OpenLitter - Open Source ESP32 Firmware for Litter Robot 1, 2 & 3
 * Copyright (C) 2024 David Lopes (https://github.com/davdlic)
 * Licensed under the GNU General Public License v3.0 - see LICENSE.
 */

(() => {
  'use strict';

  const $  = (sel, root = document) => root.querySelector(sel);
  const $$ = (sel, root = document) => Array.from(root.querySelectorAll(sel));

  let ws = null;
  let wsRetry = 1000;
  let lastStatus = null;
  let dirty = false;
  const motionStates = new Set([
    'CYCLING_CCW', 'CYCLING_DUMP_ADVANCE', 'CYCLING_DUMP_PAUSE', 'CYCLING_CW',
    'CYCLING_LEVEL_OVERSHOOT', 'CYCLING_LEVEL_RETURN',
    'CYCLING_LEVEL_BACK_OVERSHOOT', 'CYCLING_LEVEL_BACK_RETURN',
    'EMPTYING', 'EMPTYING_DUMP_ADVANCE', 'EMPTYING_DUMP_PAUSE', 'RESETTING',
  ]);

  // ---------- Toast ----------
  function toast(msg, isError = false) {
    const el = $('#toast');
    el.textContent = msg;
    el.classList.toggle('error', isError);
    el.hidden = false;
    clearTimeout(toast._t);
    toast._t = setTimeout(() => { el.hidden = true; }, 2200);
  }

  // ---------- Tabs ----------
  $$('#tabs .tab').forEach(btn => {
    btn.addEventListener('click', () => {
      $$('#tabs .tab').forEach(t => t.classList.toggle('active', t === btn));
      const target = btn.dataset.tab;
      $$('#view-dashboard, #view-settings, #view-logs').forEach(v => {
        v.classList.toggle('active', v.id === `view-${target}`);
      });
      if (target === 'logs') hydrateLogs();
    });
  });

  $$('#subtabs .subtab').forEach(btn => {
    btn.addEventListener('click', () => {
      $$('#subtabs .subtab').forEach(t => t.classList.toggle('active', t === btn));
      const sub = btn.dataset.sub;
      $$('.card.sub').forEach(c => { c.hidden = (c.dataset.sub !== sub); });
    });
  });

  // ---------- WebSocket ----------
  function connectWs() {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    ws = new WebSocket(`${proto}://${location.host}/ws`);
    ws.onmessage = e => {
      try {
        const data = JSON.parse(e.data);
        wsRetry = 1000;
        if (data && data.type === 'log') {
          appendLog(data);
        } else {
          applyStatus(data);
        }
      } catch (err) {
        console.warn('Bad WS payload', err);
      }
    };
    ws.onclose = () => {
      setTimeout(connectWs, Math.min(wsRetry, 10000));
      wsRetry *= 2;
    };
    ws.onerror = () => ws.close();
  }
  connectWs();

  // ---------- Logs ----------
  const LOG_MAX_LINES = 500;
  const logFilters = { I: true, W: true, E: true };
  let logPaused = false;
  let logHydrated = false;

  function appendLog(entry) {
    const view = $('#log-view');
    if (!view) return;
    const level = entry.level || 'I';
    if (!logFilters[level]) return;
    const ms = entry.ms || 0;
    const line = document.createElement('span');
    line.className = `log-line log-${level}`;
    line.textContent = `[${level}] ${String(ms).padStart(8)}  ${entry.msg || ''}\n`;
    view.appendChild(line);
    while (view.childElementCount > LOG_MAX_LINES) {
      view.firstElementChild.remove();
    }
    if (!logPaused) view.scrollTop = view.scrollHeight;
  }

  function parseDumpLine(line) {
    // Server format: "[L] {bootMs:>8}  message"
    const m = line.match(/^\[([IWE])\]\s+(\d+)\s+(.*)$/);
    if (!m) return null;
    return { level: m[1], ms: parseInt(m[2], 10), msg: m[3] };
  }

  async function hydrateLogs() {
    if (logHydrated) return;
    logHydrated = true;
    const view = $('#log-view');
    if (!view) return;
    try {
      const res = await fetch('/api/logs');
      const text = await res.text();
      view.innerHTML = '';
      text.split('\n').forEach(line => {
        if (!line.trim()) return;
        const e = parseDumpLine(line);
        if (e) appendLog(e);
      });
    } catch (e) {
      view.textContent = '(failed to fetch logs)';
    }
  }

  ['lf-i', 'lf-w', 'lf-e'].forEach(id => {
    const el = $('#' + id);
    if (!el) return;
    const lv = id.slice(-1).toUpperCase();
    el.addEventListener('change', () => {
      logFilters[lv] = el.checked;
      $$('#log-view .log-line').forEach(s => {
        const sl = s.className.match(/log-([IWE])/);
        if (!sl) return;
        s.hidden = !logFilters[sl[1]];
      });
    });
  });

  $('#btn-log-pause')?.addEventListener('click', () => {
    logPaused = !logPaused;
    $('#btn-log-pause').textContent = logPaused ? 'Resume' : 'Pause';
    if (!logPaused) {
      const v = $('#log-view');
      v.scrollTop = v.scrollHeight;
    }
  });

  $('#btn-log-clear')?.addEventListener('click', () => {
    $('#log-view').innerHTML = '';
  });

  $('#btn-log-download')?.addEventListener('click', () => {
    const text = $('#log-view').innerText;
    const blob = new Blob([text], { type: 'text/plain' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = `openlitter-logs-${Date.now()}.txt`;
    a.click();
    URL.revokeObjectURL(a.href);
  });

  // ---------- Status renderer ----------
  function fmtTime(secEpoch) {
    if (!secEpoch) return '—';
    const d = new Date(secEpoch * 1000);
    if (isNaN(d.getTime()) || d.getFullYear() < 2000) return '—';
    return d.toLocaleString();
  }

  function fmtDuration(s) {
    if (!s) return '—';
    if (s < 60) return `${s}s`;
    const m = Math.floor(s / 60);
    const ss = s % 60;
    return `${m}m ${ss}s`;
  }

  function fmtUptime(s) {
    if (!s) return '—';
    const d = Math.floor(s / 86400);
    const h = Math.floor((s % 86400) / 3600);
    const m = Math.floor((s % 3600) / 60);
    if (d) return `${d}d ${h}h`;
    if (h) return `${h}h ${m}m`;
    return `${m}m`;
  }

  function applyStatus(data) {
    lastStatus = data;
    const state = data.state || 'UNKNOWN';
    const badge = $('#state-badge');
    badge.textContent = stateLabel(state, data);
    badge.dataset.state = state;

    $('#state-detail').textContent = stateDescription(state, data);

    const errorBanner = $('#error-banner');
    if (data.error) {
      errorBanner.textContent = data.error;
      errorBanner.hidden = false;
    } else {
      errorBanner.hidden = true;
    }

    const globe = $('#globe');
    globe.classList.toggle('spinning', motionStates.has(state));
    globe.classList.toggle('error', state === 'ERROR');

    $('#m-cat').textContent = data.cat_present ? 'Yes' : 'No';
    $('#m-weight').textContent = data.weight_enabled
      ? `${(+data.weight_kg).toFixed(2)} kg` : 'off';
    $('#m-cycles').textContent = data.cycle_count ?? 0;
    $('#m-last').textContent = fmtTime(data.last_cycle);

    $('#sn-home').classList.toggle('active', !!data.home_position);
    $('#sn-dump').classList.toggle('active', !!data.dump_position);
    $('#sn-cat').classList.toggle('active',  !!data.cat_present);

    $('#btn-tare').hidden = !data.weight_enabled;

    const hist = $('#history');
    hist.innerHTML = '';
    (data.history || []).forEach(h => {
      const li = document.createElement('li');
      li.innerHTML = `<span class="ts">${fmtTime(h.timestamp)}</span>
                      <span>${fmtDuration(h.duration)}${h.weight_kg ? ` · ${h.weight_kg.toFixed(2)} kg` : ''}</span>`;
      hist.appendChild(li);
    });

    const net = data.network || {};
    $('#net-summary').textContent = net.ssid
      ? `${net.ssid} (${net.rssi} dBm)` : (net.ap_ip ? `AP @ ${net.ap_ip}` : 'offline');
    $('#mqtt-summary').textContent = data.mqtt_connected ? 'connected' : 'disconnected';
    $('#uptime').textContent = fmtUptime(data.uptime_sec);
    $('#version').textContent = data.version || '—';

    $('#net-status').textContent = net.mode || '—';
    $('#net-ssid').textContent   = net.ssid || '—';
    $('#net-ip').textContent     = net.ip   || '—';
    $('#net-gw').textContent     = net.gateway || '—';
    $('#net-rssi').textContent   = net.rssi ? `${net.rssi} dBm` : '—';

    $('#mqtt-status').textContent = data.mqtt_connected ? 'connected' : 'disconnected';
    $('#sys-version').textContent = data.version || '—';
    $('#sys-hostname').textContent = net.hostname || '—';
    $('#sys-uptime').textContent = fmtUptime(data.uptime_sec);
    $('#live-weight').textContent = data.weight_enabled
      ? `${(+data.weight_kg).toFixed(2)} kg` : 'off';
  }

  function stateLabel(s, data) {
    // During a Reset the firmware reuses the cycle path internally
    // (CCW -> DUMP -> overshoot -> CW), but the user-facing label should
    // reflect intent: it's a reset, not a cleaning cycle.
    if (data && data.reset_in_progress) {
      if (s === 'CYCLING_CCW' || s === 'CYCLING_DUMP_ADVANCE' ||
          s === 'CYCLING_DUMP_PAUSE' || s === 'CYCLING_CW' ||
          s === 'CYCLING_LEVEL_OVERSHOOT' || s === 'CYCLING_LEVEL_RETURN' ||
          s === 'CYCLING_LEVEL_BACK_OVERSHOOT' || s === 'CYCLING_LEVEL_BACK_RETURN' ||
          s === 'RESETTING') return 'RESETTING';
    }
    switch (s) {
      case 'IDLE':                         return 'READY';
      case 'CAT_INSIDE':                   return 'CAT INSIDE';
      case 'WAITING':                      return 'WAITING';
      case 'CYCLING_CCW':                  return 'CLEANING';
      case 'CYCLING_DUMP_ADVANCE':         return 'DUMPING';
      case 'CYCLING_DUMP_PAUSE':           return 'DUMPING';
      case 'CYCLING_CW':                   return 'RETURNING';
      case 'CYCLING_LEVEL_OVERSHOOT':      return 'LEVELING';
      case 'CYCLING_LEVEL_RETURN':         return 'LEVELING';
      case 'CYCLING_LEVEL_BACK_OVERSHOOT': return 'LEVELING';
      case 'CYCLING_LEVEL_BACK_RETURN':    return 'LEVELING';
      case 'EMPTYING':                     return 'EMPTYING';
      case 'EMPTYING_DUMP_ADVANCE':        return 'DUMPING';
      case 'EMPTYING_DUMP_PAUSE':          return 'DUMPING';
      case 'RESETTING':                    return 'RETURNING';
      case 'PAUSED':                       return 'PAUSED';
      case 'ERROR':                        return 'ERROR';
      default:                             return s;
    }
  }

  function stateDescription(s, data) {
    if (data && data.reset_in_progress) {
      switch (s) {
        case 'CYCLING_CCW':                  return 'Resetting — rotating toward dump position';
        case 'CYCLING_DUMP_PAUSE':           return 'Resetting — waste falling';
        case 'CYCLING_CW':                   return 'Resetting — returning to home';
        case 'CYCLING_LEVEL_OVERSHOOT':      return 'Resetting — levelling litter (past home)';
        case 'CYCLING_LEVEL_RETURN':         return 'Resetting — coming back to home';
        case 'CYCLING_LEVEL_BACK_OVERSHOOT': return 'Resetting — back shake';
        case 'CYCLING_LEVEL_BACK_RETURN':    return 'Resetting — back to home';
        case 'RESETTING':                    return 'Resetting — returning to home';
      }
    }
    switch (s) {
      case 'IDLE':                         return 'Ready — waiting for a cat';
      case 'CAT_INSIDE':                   return 'Cat is inside the globe';
      case 'WAITING':                      return 'Cat left, counting down before cleaning';
      case 'CYCLING_CCW':                  return 'Rotating globe toward dump position';
      case 'CYCLING_DUMP_ADVANCE':         return 'Opening dump door fully';
      case 'CYCLING_DUMP_PAUSE':           return 'Stopped at dump — waste falling into tray';
      case 'CYCLING_CW':                   return 'Returning globe to rest position';
      case 'CYCLING_LEVEL_OVERSHOOT':      return 'Levelling litter — past home (CW)';
      case 'CYCLING_LEVEL_RETURN':         return 'Levelling litter — coming back to home';
      case 'CYCLING_LEVEL_BACK_OVERSHOOT': return 'Back shake — past home (CCW)';
      case 'CYCLING_LEVEL_BACK_RETURN':    return 'Back shake — coming back to home';
      case 'EMPTYING':                     return 'Rotating to dump position';
      case 'EMPTYING_DUMP_ADVANCE':        return 'Opening dump door fully';
      case 'EMPTYING_DUMP_PAUSE':          return 'Stopped at dump — pull tray now';
      case 'RESETTING':                    return 'Returning globe to rest position';
      case 'PAUSED':                       return 'Paused — will auto-resume after grace period';
      case 'ERROR':                        return 'Error — check Logs, then press Reset';
      default:                             return '';
    }
  }

  // ---------- Controls ----------
  async function postCmd(cmd) {
    const map = {
      cycle: '/api/cycle',
      empty: '/api/empty',
      reset: '/api/reset',
      home:  '/api/home',
      pause: '/api/pause',
      resume:'/api/resume',
      tare:  '/api/tare',
    };
    const url = map[cmd];
    if (!url) return;
    try {
      const res = await fetch(url, { method: 'POST' });
      const json = await res.json();
      if (!res.ok || json.ok === false) {
        toast(json.error || 'Command failed', true);
      } else {
        toast(`${cmd} OK`);
      }
    } catch (e) {
      toast('Network error', true);
    }
  }

  document.addEventListener('click', e => {
    const cmdBtn = e.target.closest('[data-cmd]');
    if (cmdBtn) postCmd(cmdBtn.dataset.cmd);
  });

  // ---------- Settings (config) ----------
  function bindConfig(obj) {
    $$('[data-cfg]').forEach(input => {
      const k = input.dataset.cfg;
      if (!(k in obj)) return;
      const v = obj[k];
      if (input.type === 'checkbox') input.checked = !!v;
      else input.value = v;
    });
    const speedRange = document.querySelector('[data-cfg="motor_speed"]');
    if (speedRange) $('#out-speed').textContent = speedRange.value;
  }

  function readConfig() {
    const out = {};
    $$('[data-cfg]').forEach(input => {
      const k = input.dataset.cfg;
      if (input.type === 'checkbox') out[k] = input.checked;
      else if (input.type === 'number' || input.type === 'range') out[k] = +input.value;
      else out[k] = input.value;
    });
    return out;
  }

  document.addEventListener('input', e => {
    if (e.target.matches('[data-cfg]')) {
      dirty = true;
      $('#save-msg').textContent = 'Unsaved changes';
      if (e.target.matches('[data-cfg="motor_speed"]')) {
        $('#out-speed').textContent = e.target.value;
      }
    }
  });

  async function loadConfig() {
    try {
      const res = await fetch('/api/config');
      const data = await res.json();
      bindConfig(data);
    } catch (e) {
      toast('Failed to load config', true);
    }
  }
  loadConfig();

  $('#btn-save').addEventListener('click', async () => {
    try {
      const res = await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(readConfig()),
      });
      if (!res.ok) throw new Error('save failed');
      dirty = false;
      $('#save-msg').textContent = 'Saved';
      toast('Settings saved');
    } catch (e) {
      toast('Save failed', true);
    }
  });

  // ---------- Network actions ----------
  $('#btn-scan').addEventListener('click', async () => {
    toast('Scanning...');
    const res = await fetch('/api/network/scan');
    const list = await res.json();
    const ul = $('#scan-list');
    ul.innerHTML = '';
    list.sort((a, b) => b.rssi - a.rssi).forEach(n => {
      const li = document.createElement('li');
      li.innerHTML = `<span>${n.ssid || '<hidden>'}${n.secure ? ' 🔒' : ''}</span>
                      <span class="muted">${n.rssi} dBm</span>`;
      li.addEventListener('click', () => { $('#prov-ssid').value = n.ssid; });
      ul.appendChild(li);
    });
  });

  $('#btn-launch-ap').addEventListener('click', () => {
    fetch('/api/network/launch_ap', { method: 'POST' });
    toast('Recovery AP requested');
  });

  $('#btn-provision').addEventListener('click', async () => {
    const ssid = $('#prov-ssid').value.trim();
    const password = $('#prov-pwd').value;
    if (!ssid) { toast('SSID required', true); return; }
    toast('Connecting...');
    await fetch('/api/network/provision', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ ssid, password }),
    });
  });

  // ---------- System actions ----------
  $('#btn-clear-history').addEventListener('click', async () => {
    if (!confirm('Clear all history?')) return;
    await fetch('/api/history/clear', { method: 'POST' });
    toast('History cleared');
  });

  $('#btn-restart').addEventListener('click', async () => {
    if (!confirm('Restart the ESP32?')) return;
    await fetch('/api/restart', { method: 'POST' });
    toast('Restarting...');
  });

  $('#btn-factory').addEventListener('click', async () => {
    if (!confirm('Factory reset will erase WiFi and all settings. Continue?')) return;
    if (!confirm('Are you absolutely sure?')) return;
    await fetch('/api/factory_reset', { method: 'POST' });
    toast('Factory reset, restarting...');
  });

  $('#btn-export').addEventListener('click', async () => {
    const res = await fetch('/api/config');
    const data = await res.json();
    const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'openlitter-config.json';
    a.click();
    URL.revokeObjectURL(a.href);
  });

  // ---------- Firmware / filesystem update ----------
  (function setupUpdate() {
    const fileInput = $('#upd-file');
    const fileLabel = $('#upd-filename');
    const startBtn  = $('#btn-upd-start');
    const typeSel   = $('#upd-type');
    const progress  = $('#upd-progress');
    const bar       = $('#upd-bar');
    if (!fileInput || !startBtn) return;
    let chosenFile = null;

    fileInput.addEventListener('change', (e) => {
      chosenFile = e.target.files[0] || null;
      fileLabel.textContent = chosenFile ? `${chosenFile.name} (${(chosenFile.size / 1024).toFixed(1)} KB)` : 'No file selected';
      startBtn.disabled = !chosenFile;
    });

    startBtn.addEventListener('click', () => {
      if (!chosenFile) return;
      const type = typeSel.value;
      const msg = type === 'fs'
        ? 'Uploading the Web UI replaces /index.html and friends. If it fails mid-flash you may need to reflash via USB. Continue?'
        : 'Upload firmware and reboot? The device will be unreachable for ~30 seconds.';
      if (!confirm(msg)) return;

      const form = new FormData();
      form.append('update', chosenFile, chosenFile.name);
      const xhr = new XMLHttpRequest();
      xhr.open('POST', `/api/update?type=${type}`, true);
      progress.hidden = false;
      bar.style.width = '0%';
      bar.textContent = '0%';
      startBtn.disabled = true;

      xhr.upload.onprogress = (e) => {
        if (!e.lengthComputable) return;
        const pct = Math.round((e.loaded / e.total) * 100);
        bar.style.width = pct + '%';
        bar.textContent = `Uploading ${pct}%`;
      };
      xhr.onload = () => {
        let res = {};
        try { res = JSON.parse(xhr.responseText); } catch (_) {}
        if (xhr.status >= 200 && xhr.status < 300 && res.ok) {
          bar.style.width = '100%';
          bar.textContent = 'Installing...';
          toast('Upload OK, installing...');
          waitForDevice();
        } else {
          bar.textContent = 'Failed';
          toast('Update failed: ' + (res.error || `HTTP ${xhr.status}`), true);
          startBtn.disabled = false;
        }
      };
      xhr.onerror = () => {
        bar.textContent = 'Network error';
        toast('Upload error', true);
        startBtn.disabled = false;
      };
      xhr.send(form);
      toast('Uploading...');
    });

    // Poll /api/status after a reboot until the device answers again.
    function waitForDevice() {
      const MAX_SECONDS = 90;
      let elapsed = 0;
      let preBootVersion = lastStatus && lastStatus.version;
      const tick = async () => {
        try {
          const ctl = new AbortController();
          const to  = setTimeout(() => ctl.abort(), 1500);
          const r = await fetch('/api/status', { signal: ctl.signal, cache: 'no-store' });
          clearTimeout(to);
          if (r.ok) {
            let json = null;
            try { json = await r.json(); } catch (_) {}
            // Heuristic: any successful response after the reboot grace
            // means the device booted. If the version changed (firmware
            // update) or uptime is small (filesystem update reuses the
            // running firmware but still reboots), we're done.
            if (json && (!preBootVersion || json.version !== preBootVersion || (json.uptime_sec || 0) < 60)) {
              bar.textContent = 'Back online — reloading';
              toast('Device back online');
              setTimeout(() => location.reload(), 800);
              return;
            }
          }
        } catch (_) {
          // device still rebooting — keep waiting
        }
        elapsed++;
        if (elapsed > MAX_SECONDS) {
          bar.textContent = 'Timed out — reload manually';
          toast('Update timed out, try reloading', true);
          startBtn.disabled = false;
          return;
        }
        bar.textContent = `Waiting for device... (${elapsed}s)`;
        setTimeout(tick, 1000);
      };
      // Give the ESP ~3 s to write the last block, send the response,
      // and reboot before we start probing it.
      setTimeout(tick, 3000);
    }
  })();

  $('#file-import').addEventListener('change', async (e) => {
    const file = e.target.files[0];
    if (!file) return;
    try {
      const text = await file.text();
      const obj = JSON.parse(text);
      await fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(obj),
      });
      toast('Config imported');
      loadConfig();
    } catch (err) {
      toast('Invalid file', true);
    }
  });

  // Warn on unsaved changes when leaving Settings
  window.addEventListener('beforeunload', e => {
    if (dirty) { e.preventDefault(); e.returnValue = ''; }
  });

  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js').catch(() => {});
    // When a freshly-installed SW activates (after a filesystem OTA
    // update, for example), reload so we run the new assets instead of
    // whatever was in the previous cache generation.
    let swReloaded = false;
    navigator.serviceWorker.addEventListener('message', (e) => {
      if (e.data && e.data.type === 'sw-updated' && !swReloaded) {
        swReloaded = true;
        location.reload();
      }
    });
  }

  // ---------- Install banner ----------
  // Only shown on mobile-sized viewports. Desktop users can still install
  // via the browser address bar (Chrome/Edge) — no need for a popup.
  // Once dismissed in a session, never re-shows in that session, even if
  // localStorage isn't writable (incognito, restricted modes).
  (function setupInstall() {
    const banner = $('#install-banner');
    if (!banner) return;
    const hint    = $('#install-hint');
    const action  = $('#install-action');
    const close   = $('#install-close');
    const DISMISS_KEY = 'openlitter-install-dismissed';

    let dismissedSession = false;
    let deferredPrompt   = null;

    function isStandalone() {
      try {
        if (window.matchMedia('(display-mode: standalone)').matches) return true;
        if (window.matchMedia('(display-mode: fullscreen)').matches) return true;
        if (window.matchMedia('(display-mode: minimal-ui)').matches) return true;
      } catch (e) {}
      if (window.navigator.standalone === true) return true;
      if (document.referrer && document.referrer.startsWith('android-app://')) return true;
      return false;
    }

    function dismissedPersistent() {
      try { return !!localStorage.getItem(DISMISS_KEY); } catch (e) { return false; }
    }

    function isMobileViewport() {
      return window.innerWidth < 768;
    }

    function isIOSMobile() {
      const ua = navigator.userAgent || '';
      if (window.MSStream) return false;
      if (/iPad|iPhone|iPod/.test(ua)) return true;
      // iPadOS 13+ reports as "Macintosh" in UA. Only count if there's touch
      // AND we're on a mobile-shaped viewport to avoid catching Mac laptops.
      if (ua.includes('Mac') && 'ontouchend' in document && isMobileViewport()) return true;
      return false;
    }

    function shouldShow() {
      if (dismissedSession) return false;
      if (dismissedPersistent()) return false;
      if (isStandalone()) return false;
      if (!isMobileViewport()) return false;
      return true;
    }

    function hideForever() {
      dismissedSession = true;
      banner.hidden = true;
      try { localStorage.setItem(DISMISS_KEY, '1'); } catch (e) {}
    }

    function showIOSHint() {
      hint.innerHTML = 'Tap <span class="ios-share">⎘</span> then "Add to Home Screen"';
      action.hidden = true;
      banner.hidden = false;
    }

    function showChromiumPrompt() {
      hint.textContent = 'Add to your home screen for an app-like experience';
      action.hidden = false;
      banner.hidden = false;
    }

    function maybeShowIOS() {
      if (!shouldShow()) { banner.hidden = true; return; }
      if (isIOSMobile()) showIOSHint();
    }

    close.addEventListener('click', hideForever);

    window.addEventListener('beforeinstallprompt', (e) => {
      e.preventDefault();
      deferredPrompt = e;
      if (!shouldShow()) return;
      showChromiumPrompt();
    });

    action.addEventListener('click', async () => {
      if (!deferredPrompt) return;
      deferredPrompt.prompt();
      const { outcome } = await deferredPrompt.userChoice;
      deferredPrompt = null;
      hideForever();
      if (outcome !== 'accepted') {
        // User cancelled the OS prompt — still treat as "don't pester me".
      }
    });

    window.addEventListener('appinstalled', hideForever);

    // Initial decision + safety nets for iOS (where display-mode can flip
    // late) and bfcache restores. Chromium path waits for beforeinstallprompt.
    maybeShowIOS();
    window.addEventListener('pageshow', maybeShowIOS);
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) maybeShowIOS();
    });
    setTimeout(maybeShowIOS, 800);
  })();
})();
