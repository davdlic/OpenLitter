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
  const motionStates = new Set(['CYCLING_CCW', 'CYCLING_CW', 'EMPTYING', 'RESETTING']);

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
    badge.textContent = state;
    badge.dataset.state = state;

    $('#state-detail').textContent = stateDescription(state);

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

  function stateDescription(s) {
    switch (s) {
      case 'IDLE':        return 'Waiting for cat';
      case 'CAT_INSIDE':  return 'Cat detected inside';
      case 'WAITING':     return 'Counting down before cycle';
      case 'CYCLING_CCW': return 'Cleaning (forward)';
      case 'CYCLING_CW':  return 'Returning to home';
      case 'EMPTYING':    return 'Emptying...';
      case 'RESETTING':   return 'Returning to home';
      case 'PAUSED':      return 'Paused for safety';
      case 'ERROR':       return 'Manual reset required';
      default:            return '';
    }
  }

  // ---------- Controls ----------
  async function postCmd(cmd) {
    const map = {
      cycle: '/api/cycle',
      empty: '/api/empty',
      reset: '/api/reset',
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
  }

  // ---------- Install banner ----------
  // - Chromium browsers: capture beforeinstallprompt and show our own button
  //   instead of the default mini-infobar.
  // - iOS Safari: no install API exists, so show a hint pointing the user to
  //   the Share menu (Apple does not allow programmatic prompts).
  (function setupInstall() {
    const banner = $('#install-banner');
    if (!banner) return;
    const hint    = $('#install-hint');
    const action  = $('#install-action');
    const close   = $('#install-close');
    const DISMISS_KEY = 'openlitter-install-dismissed';

    const ua = navigator.userAgent || '';
    // iPadOS 13+ reports as "Macintosh" in UA, so detect via touch capability too.
    const isIOS = (/iPad|iPhone|iPod/.test(ua) ||
                   (ua.includes('Mac') && 'ontouchend' in document)) &&
                  !window.MSStream;

    function detectStandalone() {
      try {
        if (window.matchMedia('(display-mode: standalone)').matches) return true;
        if (window.matchMedia('(display-mode: fullscreen)').matches) return true;
        if (window.matchMedia('(display-mode: minimal-ui)').matches) return true;
      } catch (e) {}
      if (window.navigator.standalone === true) return true;
      if (document.referrer && document.referrer.startsWith('android-app://')) return true;
      return false;
    }

    function dismissed() {
      try { return !!localStorage.getItem(DISMISS_KEY); } catch (e) { return false; }
    }

    function hideBanner() {
      banner.hidden = true;
    }

    function maybeShow() {
      if (detectStandalone() || dismissed()) { hideBanner(); return; }
      if (isIOS) {
        hint.innerHTML = 'Tap <span class="ios-share">⎘</span> then "Add to Home Screen"';
        action.hidden = true;
        banner.hidden = false;
      }
      // Chromium path is gated on beforeinstallprompt below.
    }

    close.addEventListener('click', () => {
      hideBanner();
      try { localStorage.setItem(DISMISS_KEY, '1'); } catch (e) {}
    });

    let deferredPrompt = null;
    window.addEventListener('beforeinstallprompt', (e) => {
      e.preventDefault();
      deferredPrompt = e;
      if (detectStandalone() || dismissed()) return;
      hint.textContent = 'Add to your home screen for an app-like experience';
      action.hidden = false;
      banner.hidden = false;
    });

    action.addEventListener('click', async () => {
      if (!deferredPrompt) return;
      deferredPrompt.prompt();
      const { outcome } = await deferredPrompt.userChoice;
      deferredPrompt = null;
      action.hidden = true;
      if (outcome === 'accepted') hideBanner();
    });

    window.addEventListener('appinstalled', () => {
      hideBanner();
      deferredPrompt = null;
      try { localStorage.setItem(DISMISS_KEY, '1'); } catch (e) {}
    });

    // Initial decision + safety nets:
    //  - pageshow fires when restored from bfcache, common on iOS PWAs.
    //  - visibilitychange catches when the user re-opens the standalone app.
    //  - delayed re-check handles iOS quirks where display-mode reports
    //    non-standalone on first paint then flips after the runtime kicks in.
    maybeShow();
    window.addEventListener('pageshow', maybeShow);
    document.addEventListener('visibilitychange', () => {
      if (!document.hidden) maybeShow();
    });
    setTimeout(maybeShow, 800);
  })();
})();
