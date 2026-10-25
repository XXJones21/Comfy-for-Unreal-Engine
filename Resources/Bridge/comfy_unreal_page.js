// Copyright Comfy Org. All Rights Reserved.
//
// Comfy for Unreal page bridge. Comfy Studio injects this into the ComfyUI frontend shown in its Graph
// view (Comfy Cloud, or a local ComfyUI) after every page load. It:
//   - reports the signed-in Comfy account, free runs and credits to Unreal (window.ue.comfyunreal.reportaccount),
//     again after every run and once a minute;
//   - runs calls for the plugin (window.__comfyUnreal.call) with the page's own sign-in, so Comfy Cloud
//     works without an API key and the account's token never leaves the page.
// It only uses the frontend's public API object (window.comfyAPI.api.api) and reads the account stores.
(() => {
  'use strict';
  const VERSION = 1;
  if (window.__comfyUnreal && window.__comfyUnreal.version === VERSION) {
    window.__comfyUnreal.reportAccount(true);
    return;
  }

  const bridge = () => window.ue && window.ue.comfyunreal;
  const api = () => window.comfyAPI && window.comfyAPI.api && window.comfyAPI.api.api;
  const pinia = () => {
    const root = document.querySelector('#vue-app');
    const app = root && root.__vue_app__;
    return app && app.config && app.config.globalProperties && app.config.globalProperties.$pinia;
  };
  const store = (id) => {
    const p = pinia();
    return p && p._s && p._s.get(id);
  };
  const log = (message) => {
    try { const b = bridge(); if (b) b.log(String(message)); } catch (e) { /* the bridge isn't bound yet */ }
  };
  const describe = (e) => {
    if (!e) return 'Unknown error';
    // PromptExecutionError carries ComfyUI's validation response.
    const r = e.response;
    if (r && (r.error || r.node_errors)) {
      let text = (r.error && (r.error.message || r.error.type)) || e.message || 'The workflow was rejected';
      if (r.error && r.error.details) text += ': ' + r.error.details;
      for (const [node, info] of Object.entries(r.node_errors || {})) {
        for (const err of (info && info.errors) || []) {
          text += `\n  Node ${node} (${info.class_type}): ${err.message} ${err.details || ''}`;
        }
      }
      return text;
    }
    return e.message || String(e);
  };
  const errorText = async (response) => {
    let body = '';
    try { body = await response.text(); } catch (e) { /* no body */ }
    try {
      const json = JSON.parse(body);
      const message = (json.error && (json.error.message || json.error)) || json.message;
      if (message) return `${response.status}: ${message}`;
    } catch (e) { /* not JSON */ }
    return `${response.status} ${response.statusText}${body ? ': ' + body.slice(0, 300) : ''}`;
  };
  const toBase64 = (bytes) => {
    let binary = '';
    const chunk = 0x8000;
    for (let i = 0; i < bytes.length; i += chunk) {
      binary += String.fromCharCode.apply(null, bytes.subarray(i, i + chunk));
    }
    return btoa(binary);
  };
  const fromBase64 = (text) => {
    const binary = atob(text);
    const bytes = new Uint8Array(binary.length);
    for (let i = 0; i < binary.length; i++) bytes[i] = binary.charCodeAt(i);
    return bytes;
  };

  // Account ----------------------------------------------------------------------------------

  let lastReport = '';
  let loggedShapes = false;

  async function readAccount() {
    const auth = store('auth');
    const comfy = api();
    if (!auth || !comfy) {
      return null; // Not the ComfyUI app, or it hasn't started yet.
    }
    const account = { signedIn: !!auth.isAuthenticated };
    if (!account.signedIn) {
      return account;
    }
    const user = auth.currentUser || auth.sessionUser || {};
    account.displayName = user.displayName || user.name || '';
    account.email = auth.userEmail || user.email || '';
    const workspaces = store('teamWorkspace');
    const active = workspaces && workspaces.activeWorkspace;
    if (active && active.type && active.type !== 'personal') {
      account.workspace = active.name || '';
    }

    const shapes = {};
    try {
      const response = await comfy.fetchApi('/features');
      shapes.features = response.status;
      if (response.ok) {
        const features = await response.json();
        const free = features && features.free_tier_balance;
        if (free) {
          account.freeRunsRemaining = Number(free.remaining ?? 0);
          account.freeRunsAllowance = Number(free.allowance ?? 0);
        }
      }
    } catch (e) { shapes.features = describe(e); }

    // Workspace billing first, then the account's own balance (older billing).
    let micros;
    try {
      const response = await comfy.fetchApi('/billing/balance');
      shapes.billing = response.status;
      if (response.ok) {
        const balance = await response.json();
        micros = balance.effective_balance_micros ?? balance.amount_micros;
        shapes.billingKeys = Object.keys(balance || {}).join(',');
      }
    } catch (e) { shapes.billing = describe(e); }
    if (typeof micros !== 'number' && typeof auth.fetchBalance === 'function') {
      try {
        const balance = await auth.fetchBalance();
        if (balance) {
          micros = balance.effective_balance_micros ?? balance.amount_micros;
          shapes.customerKeys = Object.keys(balance).join(',');
        }
      } catch (e) { shapes.customer = describe(e); }
    }
    if (typeof micros === 'number') {
      // Comfy credits: 211 per US dollar; balances are in millionths of a dollar.
      account.credits = Math.floor((micros / 1e6) * 211);
    }
    if (!loggedShapes) {
      loggedShapes = true;
      log(`account read: features ${shapes.features}, billing ${shapes.billing} [${shapes.billingKeys || ''}], customer [${shapes.customerKeys || shapes.customer || ''}], free runs ${account.freeRunsRemaining ?? '-'}/${account.freeRunsAllowance ?? '-'}, credits ${account.credits ?? '-'}`);
    }
    return account;
  }

  async function reportAccount(force) {
    const account = await readAccount();
    if (!account) return;
    const text = JSON.stringify(account);
    if (!force && text === lastReport) return;
    lastReport = text;
    const b = bridge();
    if (b) b.reportaccount(text);
  }

  // Calls from Unreal ------------------------------------------------------------------------

  const jobs = new Map(); // prompt id -> latest progress

  const methods = {
    async objectInfo() {
      const response = await api().fetchApi('/object_info', { cache: 'no-store' });
      if (!response.ok) throw new Error(await errorText(response));
      return await response.json();
    },
    async upload({ name, mime, data }) {
      const form = new FormData();
      form.append('image', new Blob([fromBase64(data)], { type: mime || 'application/octet-stream' }), name);
      form.append('type', 'input');
      form.append('subfolder', 'unreal');
      form.append('overwrite', 'true');
      const response = await api().fetchApi('/upload/image', { method: 'POST', body: form });
      if (!response.ok) throw new Error(await errorText(response));
      return await response.json();
    },
    async queue({ prompt, workflow }) {
      const result = await api().queuePrompt(0, { output: prompt, workflow: workflow || {} });
      jobs.set(result.prompt_id, null);
      return result;
    },
    async status({ prompt_id }) {
      const detail = await api().getJobDetail(prompt_id);
      const progress = jobs.get(prompt_id) || undefined;
      if (!detail) return { status: 'pending', progress };
      return { status: detail.status, outputs: detail.outputs, execution_error: detail.execution_error, progress };
    },
    async fetch({ filename, subfolder, type }) {
      const query = new URLSearchParams({ filename, subfolder: subfolder || '', type: type || 'output' });
      const response = await api().fetchApi('/view?' + query.toString());
      if (!response.ok) throw new Error(await errorText(response));
      return { data: toBase64(new Uint8Array(await response.arrayBuffer())) };
    },
    async cancel({ prompt_id }) {
      const comfy = api();
      if (typeof comfy.cancelJob === 'function') await comfy.cancelJob(prompt_id);
      else await comfy.interrupt(prompt_id);
      return {};
    },
    async account() {
      await reportAccount(true);
      return {};
    },
  };

  async function call(id, method, args) {
    const b = bridge();
    try {
      if (!api()) throw new Error('The ComfyUI page has not finished loading.');
      const fn = methods[method];
      if (!fn) throw new Error('Unknown bridge method ' + method);
      const result = await fn(args || {});
      if (b) b.reply(id, true, JSON.stringify(result ?? {}), '');
    } catch (e) {
      if (b) b.reply(id, false, '', describe(e));
    }
  }

  // Wiring -----------------------------------------------------------------------------------

  let listening = false;
  function listen() {
    const comfy = api();
    if (listening || !comfy || typeof comfy.addEventListener !== 'function') return;
    listening = true;
    comfy.addEventListener('progress', (event) => {
      const d = event.detail || {};
      if (jobs.has(d.prompt_id)) jobs.set(d.prompt_id, { value: d.value, max: d.max, node: String(d.node ?? '') });
    });
    // Any finished run, from Unreal or from the graph here, changes free runs or credits.
    const refreshSoon = () => {
      setTimeout(() => reportAccount(false), 1500);
      setTimeout(() => reportAccount(false), 8000);
    };
    for (const name of ['execution_success', 'execution_error', 'execution_interrupted']) {
      comfy.addEventListener(name, refreshSoon);
    }
  }

  window.__comfyUnreal = { version: VERSION, call, reportAccount };

  // Watch for sign-in quickly, then keep the account fresh.
  let ticks = 0;
  setInterval(() => {
    listen();
    ticks += 1;
    const auth = store('auth');
    const signedIn = !!(auth && auth.isAuthenticated);
    const wasSignedIn = lastReport.includes('"signedIn":true');
    if (signedIn !== wasSignedIn || !lastReport || ticks % 30 === 0) {
      reportAccount(false);
    }
  }, 2000);
  window.addEventListener('focus', () => reportAccount(false));
  listen();
  reportAccount(true);
  log(`page bridge ${VERSION} running at ${location.origin}`);
})();
