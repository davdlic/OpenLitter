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
      $$('#view-dashboard, #view-settings').forEach(v => {
        v.classList.toggle('active', v.id === `view-${target}`);
      });
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
        applyStatus(data);
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
})();
