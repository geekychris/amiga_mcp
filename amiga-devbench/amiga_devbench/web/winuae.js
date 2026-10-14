// SPDX-License-Identifier: MIT
// Native WinUAE UI. HTTP and MCP operate on the same server-owned RSP session.
(() => {
  const tab = document.createElement('div');
  tab.className = 'tab'; tab.dataset.tab = 'winuae'; tab.textContent = 'WinUAE';
  document.querySelector('[data-tab="settings"]').before(tab);
  const panel = document.createElement('div');
  panel.className = 'tab-panel'; panel.id = 'panel-winuae';
  panel.innerHTML = `
    <style>
      #panel-winuae { overflow:auto; }
      .wu-row { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin:10px 0; }
      .wu-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(min(100%,400px),1fr)); gap:14px; align-items:start; }
      .wu-card { background:var(--bg-card); border:1px solid var(--border); border-radius:6px; padding:14px; min-width:0; }
      .wu-card h3 { margin:0 0 10px; font-size:14px; }
      #panel-winuae input, #panel-winuae select, #panel-winuae textarea { color:var(--text-primary); background:var(--bg-input); border:1px solid var(--border); border-radius:4px; padding:6px; max-width:100%; }
      #panel-winuae input { width:140px; }
      #panel-winuae pre { overflow:auto; font-size:12px; line-height:1.6; max-height:360px; white-space:pre; }
      #panel-winuae button:disabled { opacity:.45; cursor:default; }
      #wu-error { color:#ff9090; white-space:pre-wrap; }
      .wu-note { color:var(--text-secondary); font-size:12px; line-height:1.6; }
      #wu-points { width:100%; font:12px monospace; text-align:left; }
      #wu-points td { padding:5px; }
    </style>
    <h2>WinUAE native debugger</h2>
    <p class="wu-note">Inspect the emulator directly, including a crashed guest. Connecting pauses the CPU (WinUAE GDB attach behavior).
      Pause before reading or editing state. Disconnect releases this session's breakpoints and resumes execution.</p>
    <p id="wu-setup" class="wu-note" hidden>Enable <code>[winuae_debug] enabled = true</code> and set its GDB port in your local DevBench config, then restart DevBench.
      WinUAE needs <code>debugging_features=gdbserver</code> and a matching <code>gdb_port</code>.
      Only one debugger can use that port at a time.</p>
    <div class="wu-row">
      <strong id="wu-status" role="status">Loading configuration…</strong>
      <button class="btn" id="wu-connect" data-wu="connect">Connect &amp; pause</button>
      <button class="btn" data-wu="disconnect" data-connected>Disconnect</button>
      <button class="btn" id="wu-pause" data-wu="pause" data-connected>Pause</button>
      <button class="btn" data-wu="resume" data-stopped>Resume</button>
      <button class="btn" data-wu="step" data-stopped>Step</button>
      <button class="btn" data-wu="step_over" data-stopped data-cap="step-over">Step over</button>
      <button class="btn" data-wu="refresh" data-stopped>Refresh CPU</button>
    </div>
    <div id="wu-error" role="alert"></div>
    <div class="wu-grid">
      <section class="wu-card"><h3>CPU registers</h3>
        <p class="wu-note">Values and disassembly reflect the last inspection while stopped.</p>
        <pre id="wu-registers">Pause to inspect registers.</pre>
        <div class="wu-row"><label>Register <select id="wu-reg"></select></label>
          <label>Value <input id="wu-value" placeholder="0x00000000"></label>
          <button class="btn" data-wu="write-register" data-stopped>Write register</button></div>
      </section>
      <section class="wu-card"><h3>Disassembly</h3>
        <div class="wu-row"><label>Address <input id="wu-disaddr" value="pc"></label>
          <button class="btn" data-wu="disassemble" data-stopped data-cap="disasm">Disassemble</button></div>
        <pre id="wu-disassembly">Pause to disassemble.</pre>
      </section>
      <section class="wu-card"><h3>Breakpoints and watchpoints</h3>
        <div class="wu-row"><label>Address <input id="wu-bpaddr" placeholder="0x00010000"></label>
          <label>Kind <select id="wu-kind"><option value="execute">Execute</option><option value="write">Write</option><option value="read">Read</option><option value="access">Read/write</option></select></label>
          <label>Bytes <input id="wu-bpsize" type="number" min="1" max="65536" value="1"></label>
          <button class="btn" data-wu="add-point" data-stopped>Add</button></div>
        <table id="wu-points"><thead><tr><th>Address</th><th>Kind</th><th>Size</th><th></th></tr></thead><tbody></tbody></table>
        <p class="wu-note">Only points owned by this connection are listed. Execute breakpoints use an even instruction address.</p>
      </section>
      <section class="wu-card"><h3>Physical memory</h3>
        <div class="wu-row"><label>Address <input id="wu-memaddr" value="0x0"></label>
          <label>Bytes <input id="wu-memsize" type="number" value="256" min="1" max="65536"></label>
          <button class="btn" data-wu="read-memory" data-stopped>Read</button></div>
        <pre id="wu-memory">Pause to read memory.</pre>
        <label>RAM write (hex bytes)<br><textarea id="wu-memdata" rows="2" placeholder="4e714e71"></textarea></label>
        <button class="btn" data-wu="write-memory" data-stopped data-cap="memory-check">Write RAM</button>
        <p class="wu-note">Addresses and values accept decimal, 0x-prefixed hex or $-prefixed hex. RAM writes reject ROM and I/O.</p>
      </section>
      <section class="wu-card"><h3>Program entry and exception stops</h3>
        <div class="wu-row"><label>Program name <input id="wu-program" placeholder="myprogram"></label>
          <button class="btn" data-wu="arm-process" data-stopped data-cap="process-break">Arm entry stop</button>
          <button class="btn" data-wu="clear-process" data-stopped data-cap="process-break">Clear entry stop</button></div>
        <p class="wu-note">Arm while paused, then resume and launch the program in the guest.</p>
        <div class="wu-row"><label>Exception vectors <input id="wu-vectors" value="2,3,4,5,8,10,11"></label>
          <button class="btn" data-wu="exceptions" data-stopped data-cap="exception-mask">Set stops</button>
          <button class="btn" data-wu="clear-exceptions" data-stopped data-cap="exception-mask">Disable stops</button></div>
        <pre id="wu-stop-settings"></pre>
      </section>
      <section class="wu-card"><h3>Crash and process inspection</h3>
        <div class="wu-row">
          <button class="btn" data-wu="tasks" data-stopped data-cap="tasks">Tasks</button>
          <button class="btn" data-wu="exception" data-stopped data-cap="exception">Last exception</button>
          <button class="btn" data-wu="memory-map" data-stopped data-cap="memory-map">Memory map</button>
          <label>Process address <input id="wu-process" value="0" title="0 means current process"></label>
          <button class="btn" data-wu="segments" data-stopped data-cap="segments">Loaded segments</button>
        </div>
        <pre id="wu-inspection">These inspections do not require the guest bridge.</pre>
      </section>
    </div>`;
  document.querySelector('.content').append(panel);
  const el = id => document.getElementById('wu-' + id);
  const hex = n => '0x' + Number(n).toString(16).padStart(8, '0');
  const kinds = {0:'execute', 2:'write', 3:'read', 4:'access'};
  let state = {connected:false, enabled:false, capabilities:{}};
  let busy = false;
  for (const name of [...Array.from({length:8}, (_,i)=>'D'+i), ...Array.from({length:8}, (_,i)=>'A'+i), 'SR','PC']) {
    const option = document.createElement('option'); option.textContent = name; el('reg').append(option);
  }
  async function request(action, args = {}) {
    const response = await fetch('/api/winuae/' + action, action === 'status' ? {} : {
      method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(args)
    });
    const body = await response.json();
    if (!response.ok || !body.ok) throw new Error(body.error || 'Debugger request failed');
    return body.result;
  }
  function controls() {
    for (const button of panel.querySelectorAll('button')) {
      const capability = button.dataset.cap;
      button.disabled = busy ||
        (button.hasAttribute('data-connected') && !state.connected) ||
        (button.hasAttribute('data-stopped') && (!state.connected || state.execution !== 'stopped')) ||
        (capability && !(state.capabilities.commands || []).includes(capability));
    }
    el('connect').disabled = busy || !state.enabled || state.connected;
    el('pause').disabled = busy || !state.connected || state.execution === 'stopped';
  }
  function render() {
    el('status').textContent = `${state.host || ''}:${state.port || ''} — ${state.execution}` +
      (state.last_stop ? ` (${state.last_stop})` : '') + (state.last_error ? ` — ${state.last_error}` : '');
    el('setup').hidden = state.enabled;
    const tbody = el('points').querySelector('tbody'); tbody.replaceChildren();
    for (const point of state.breakpoints || []) {
      const row = document.createElement('tr');
      for (const value of [hex(point.address), kinds[point.kind], point.size]) {
        const cell = document.createElement('td'); cell.textContent = value; row.append(cell);
      }
      const cell = document.createElement('td'), button = document.createElement('button');
      button.className = 'btn'; button.textContent = 'Remove'; button.setAttribute('data-stopped', '');
      button.addEventListener('click', () => perform(async () => {
        await request('breakpoint', {operation:'remove', address:point.address, kind:kinds[point.kind], size:point.size});
      }));
      cell.append(button); row.append(cell); tbody.append(row);
    }
    controls();
  }
  async function cpu() {
    if (state.execution !== 'stopped' || !state.connected) return;
    const registers = await request('registers');
    el('registers').textContent = Array.from({length:8}, (_,i)=>`D${i} ${hex(registers['D'+i])}    A${i} ${hex(registers['A'+i])}`).join('\n') +
      `\nSR ${hex(registers.SR)}    PC ${hex(registers.PC)}`;
    if ((state.capabilities.commands || []).includes('disasm'))
      el('disassembly').textContent = (await request('disassemble', {address:el('disaddr').value, count:20})).text;
  }
  async function refresh() {
    const previous = state.execution;
    state = await request('status'); render();
    if (previous !== 'stopped' && state.execution === 'stopped') await cpu();
  }
  async function perform(operation, clearError = true) {
    if (busy) return;
    busy = true; if (clearError) el('error').textContent = ''; controls();
    try { await operation(); await refresh(); }
    catch (error) { el('error').textContent = error.message; }
    finally { busy = false; controls(); }
  }
  panel.addEventListener('click', event => {
    const action = event.target.closest('[data-wu]')?.dataset.wu;
    if (!action) return;
    perform(async () => {
      if (['connect','disconnect','pause','resume','step','step_over'].includes(action)) {
        state = await request(action); render();
        if (action === 'step') state = await request('wait', {timeout:5});
        if (state.execution === 'stopped') await cpu();
        return;
      }
      if (action === 'refresh') return cpu();
      if (action === 'write-register') {
        await request('registers', {register:el('reg').value, value:el('value').value}); return cpu();
      }
      if (action === 'disassemble') {
        el('disassembly').textContent = (await request('disassemble', {address:el('disaddr').value})).text; return;
      }
      if (action === 'add-point') return request('breakpoint', {operation:'add', address:el('bpaddr').value, kind:el('kind').value, size:Number(el('bpsize').value)});
      if (action === 'read-memory') {
        const data = await request('read_memory', {address:el('memaddr').value, size:Number(el('memsize').value)});
        el('memory').textContent = Array.from({length:Math.ceil(data.size/16)}, (_,i)=>`${hex(data.address+i*16)}  ${data.hex.slice(i*32,i*32+32).match(/../g).join(' ')}`).join('\n'); return;
      }
      if (action === 'write-memory') return request('write_memory', {address:el('memaddr').value, hex_data:el('memdata').value});
      if (action === 'arm-process' || action === 'clear-process') {
        await request('process_break', {operation:action === 'arm-process' ? 'name' : 'clear', name:el('program').value});
        el('stop-settings').textContent = JSON.stringify(await request('process_break'), null, 2); return;
      }
      if (action === 'exceptions' || action === 'clear-exceptions') {
        const vectors = action === 'clear-exceptions' || !el('vectors').value.trim() ? [] : el('vectors').value.split(',').map(x=>Number(x.trim()));
        el('stop-settings').textContent = JSON.stringify(await request('exception_mask', {vectors}), null, 2); return;
      }
      el('inspection').textContent = JSON.stringify(await request('inspect', {subject:action, process:el('process').value}), null, 2);
    });
  });
  tab.addEventListener('click', () => perform(async () => {}));
  setInterval(() => {
    if (!document.hidden && panel.classList.contains('active') && !busy) perform(async () => {}, false);
  }, 1500);
  perform(async () => {});
})();
