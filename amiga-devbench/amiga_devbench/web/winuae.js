// SPDX-License-Identifier: MIT
// Native WinUAE UI. HTTP and MCP operate on the same server-owned RSP session.
(() => {
  const tab = document.createElement('div');
  tab.className = 'tab'; tab.dataset.tab = 'winuae'; tab.textContent = 'WinUAE';
  document.querySelector('[data-tab="settings"]').before(tab);
  const panel = document.createElement('div');
  panel.className = 'tab-panel'; panel.id = 'panel-winuae';
  const input = (id, attrs = '') => `<input id="wu-${id}" ${attrs}>`;
  const button = (action, label, flags = 'data-stopped', cap = '') =>
    `<button class="btn" data-wu="${action}" ${flags}${cap ? ` data-cap="${cap}"` : ''}>${label}</button>`;
  const card = (title, body) => `<section class="wu-card"><h3>${title}</h3>${body}</section>`;
  const row = body => `<div class="wu-row">${body}</div>`;
  const out = (id, text = '') => `<pre id="wu-${id}">${text}</pre>`;
  const sources = ['cpu', 'blitter', 'copper', 'disk', 'audio', 'bitplane', 'sprite']
    .map(s => `<label><input type="checkbox" class="wu-src" value="${s}"${s === 'cpu' ? ' checked' : ''}> ${s}</label>`).join(' ');
  const subtabs = {
    cpu: ['CPU &amp; breakpoints', `<div class="wu-grid">
      ${card('Disassembly', row(`<label>Address ${input('disaddr', 'value="pc"')}</label>
          <label>Count ${input('discount', 'type="number" value="24" min="1" max="256"')}</label>
          ${button('disassemble', 'Disassemble', 'data-stopped', 'disasm')}`) +
        row(`<label><input type="checkbox" id="wu-annotate" checked> Name library calls</label>
          <label>Library ${input('library', 'placeholder="from A6"')}</label>
          <label><input type="checkbox" id="wu-xref"> Source lines</label>
          <label>Project ${input('project', 'placeholder="any loaded"')}</label>`) + out('disassembly', 'Pause to disassemble.'))}
      ${card('CPU registers', `<p class="wu-note">Click a value to edit it. USP, ISP and MSP are read by switching SR briefly.</p>
        ${out('registers', 'Pause to inspect registers.')}` + row(`<label>Register <select id="wu-reg"></select></label>
          <label>Value ${input('value', 'placeholder="0x00000000"')}</label>${button('write-register', 'Write register')}`))}
      ${card('Breakpoints', row(`<label>Address or symbol ${input('bpaddr', 'placeholder="0x00010000 or main"')}</label>
          <label>Skip ${input('bpskip', 'type="number" min="0" value="0"')}</label>
          <label><input type="checkbox" id="wu-oneshot"> One-shot</label>`) +
        row(`${button('add-bp', '+ At address')}${button('add-bp-symbol', '+ At symbol')}${button('clear-bps', 'Clear all')}`) +
        `<table class="wu-table" id="wu-points"><thead><tr><th>Address</th><th>Kind</th><th>Size</th><th>Hits</th><th>Skip</th><th></th></tr></thead><tbody></tbody></table>
        <p class="wu-note">Skips apply while resuming; a one-shot breakpoint removes itself after it stops. Points survive checkpoint restores and resets.</p>`)}
      ${card('Watchpoints', row(`<label>Address ${input('wpaddr', 'placeholder="0x00020000"')}</label>
          <label>Bytes ${input('wpsize', 'type="number" min="1" max="65536" value="4"')}</label>
          <label>Access <select id="wu-wprwi"><option>W</option><option>R</option><option selected>RW</option><option>RWI</option><option>I</option></select></label>`) +
        row(`<label>Value ${input('wpvalue', 'placeholder="optional"')}</label><label>Mask ${input('wpmask', 'placeholder="optional"')}</label>
          <label><input type="checkbox" id="wu-mustchange"> Changes only</label><label><input type="checkbox" id="wu-exact"> Exact access</label>
          <label><input type="checkbox" id="wu-logonly"> Log only</label>`) +
        row(`Sources: ${sources}`) + row(`${button('add-wp', '+ Watch', 'data-stopped', 'watch')}${button('clear-wps', 'Clear all', 'data-stopped', 'watch')}
          ${button('watch-events', 'Access log', 'data-stopped', 'watch')}`) + out('watches', 'No watchpoints.') +
        `<p class="wu-note">A range matches any overlapping access; exact matches one aligned 1, 2 or 4 byte access, and chip RAM is 16 bits wide.
          Values need 1, 2 or 4 bytes. Instruction fetches are seen when the CPU emulation fetches through memory banks, such as the compatible 68000.</p>`)}
    </div>`],
    memory: ['Memory', `<div class="wu-grid">
      ${card('Physical memory', row(`<button class="btn" data-wu="mem-back" id="wu-back" disabled>&larr;</button>
          <label>Address ${input('memaddr', 'value="0x0"')}</label><label>Bytes ${input('memsize', 'type="number" value="256" min="1" max="65536"')}</label>
          <label>Show <select id="wu-memformat"><option>bytes</option><option>words</option><option selected>longs</option><option>ascii</option></select></label>
          ${button('read-memory', 'Read')}`) + `<div id="wu-memory" class="wu-mono">Pause to read memory. Click a longword to follow it.</div>` +
        row(`<label>RAM write ${input('memdata', 'placeholder="hex bytes, e.g. 4e714e71" style="width:260px"')}</label>${button('write-memory', 'Write RAM', 'data-stopped', 'memory-check')}`) +
        `<p class="wu-note">Addresses and values accept decimal, 0x-prefixed hex or $-prefixed hex. RAM writes reject ROM and I/O.</p>`)}
      ${card('Memory map', row(button('memory-map', 'Refresh', 'data-stopped', 'memory-map')) + out('memmap', 'Pause, then refresh.'))}
      ${card('Stack (from A7)', row(`<label>Longwords ${input('stackdepth', 'type="number" value="16" min="1" max="1024"')}</label>${button('stack', 'Read')}`) + out('stack', 'Pause, then read.'))}
      ${card('Search', row(`<label>From ${input('findaddr', 'value="0x0"')}</label><label>Bytes ${input('findlen', 'value="0x200000"')}</label>
          <label>Hex ${input('findhex', 'placeholder="4e75"')}</label>${button('search', 'Search')}`) + out('found'))}
    </div>`],
    chipset: ['Chipset', `<div class="wu-grid">
      ${card('Custom chips', row(button('custom', 'Refresh')) + out('custom', 'Pause, then refresh. Write-only registers come from WinUAE\'s saved custom state.'))}
      ${card('Copper list', row(`<label>Address ${input('copaddr', 'value="cop1lc"')}</label><label>Count ${input('copcount', 'type="number" value="64" min="1" max="4096"')}</label>${button('copper', 'Decode')}`) + out('copper'))}
      ${card('Screenshot', row(button('screenshot', 'Capture', 'data-stopped', 'screenshot')) + `<div id="wu-screen"></div>`)}
      ${card('Bitplanes', row(`<label>Width ${input('bmw', 'type="number" value="320"')}</label><label>Height ${input('bmh', 'type="number" value="256"')}</label>
          <label>Row bytes ${input('bmstride', 'placeholder="auto"')}</label>`) + row(`<label>Planes ${input('bmplanes', 'placeholder="0x20000,0x22800" style="width:240px"')}</label>`) +
        row(`<label>Palette ${input('bmpalette', 'placeholder="0x000000,0xffffff" style="width:240px"')}</label>${button('bitmap', 'Decode')}`) + `<div id="wu-bitmap"></div>`)}
    </div>`],
    state: ['State &amp; symbols', `<div class="wu-grid">
      ${card('Snapshot slots', `<p class="wu-note">Quick saves under <code>~/.amiga-devbench/snapshots/winuae</code>. WinUAE saves at the next frame start; this session's breakpoints are lifted for that run and reinstalled after a load.</p>
        <table class="wu-table" id="wu-slots"><tbody></tbody></table>` + row(`<label>Path ${input('statepath', 'placeholder="/tmp/state.uss" style="width:240px"')}</label>
          ${button('state-save', 'Save', 'data-stopped', 'checkpoint')}${button('state-load', 'Load', 'data-stopped', 'checkpoint')}`) + out('statemsg'))}
      ${card('Automatic snapshots', `<p class="wu-note">Saves to a rotating ring every interval; each save pauses the guest briefly.</p>` +
        row(`<label>Seconds ${input('autoint', 'type="number" min="0" max="3600" value="0"')}</label><label>Ring ${input('autoring', 'type="number" min="1" max="64" value="5"')}</label>
          ${button('autosnap', 'Apply', 'data-connected')}${button('autosnap-off', 'Disable', '')}`) + out('autosnap', 'off'))}
      ${card('Compare snapshots', row(`<select id="wu-diffa"></select> vs <select id="wu-diffb"></select>${button('diff', 'Compare', '')}`) + out('diff'))}
      ${card('Address lookup', row(`<label>Address ${input('lookaddr', 'placeholder="0xDFF096"')}</label>${button('lookup', 'Look up', '')}`) + out('looked'))}
      ${card('Library offsets', row(`<label>Offset ${input('fdoffset', 'placeholder="-552"')}</label><label>Library ${input('fdlib', 'value="exec"')}</label>${button('fd-lookup', 'Look up', '')}
          ${button('fd-libraries', 'Libraries', '')}`) + row(`<label>FD file ${input('fdpath', 'placeholder="/path/graphics_lib.fd" style="width:240px"')}</label>${button('fd-load', 'Load', '')}`) + out('fd'))}
    </div>`],
    watches: ['Watches &amp; stops', `<div class="wu-grid">
      ${card('DMA watches', row(`<label>Address ${input('dmaaddr')}</label><label>Bytes ${input('dmalen', 'type="number" value="16" min="1" max="65536"')}</label>
          <label>Mode <select id="wu-dmamode"><option>write</option><option>read</option><option>access</option></select></label>`) +
        row(['blitter', 'copper', 'disk', 'audio', 'bitplane', 'sprite'].map(s => `<label><input type="checkbox" class="wu-dmasrc" value="${s}" checked> ${s}</label>`).join(' ')) +
        row(`${button('dma-add', '+ DMA watch', 'data-stopped', 'dma-watch')}${button('dma-list', 'List', 'data-stopped', 'dma-watch')}
          <label>ID ${input('dmaid', 'type="number" min="0" style="width:60px"')}</label>${button('dma-remove', 'Remove', 'data-stopped', 'dma-watch')}`) + out('dma'))}
      ${card('Register conditions', row(`<label>Register <select id="wu-condreg">${['D0','D1','D2','D3','D4','D5','D6','D7','A0','A1','A2','A3','A4','A5','A6','A7','USP','MSP','ISP','VBR','SR'].map(r => `<option>${r}</option>`).join('')}</select></label>
          <label><select id="wu-condop">${['eq','ne','le','ge','lt','gt'].map(o => `<option>${o}</option>`).join('')}</select></label>
          <label>Value ${input('condvalue', 'value="0"')}</label><label><input type="checkbox" id="wu-condsigned"> Signed</label>`) +
        row(`${button('cond-add', '+ Condition', 'data-stopped', 'condition')}${button('cond-list', 'List', 'data-stopped', 'condition')}
          <label>ID ${input('condid', 'type="number" min="0" style="width:60px"')}</label>${button('cond-remove', 'Remove', 'data-stopped', 'condition')}`) + out('conditions'))}
      ${card('PC history', row(`${button('history-on', 'Record', 'data-stopped', 'history')}${button('history-off', 'Stop', 'data-stopped', 'history')}
          <label>Entries ${input('histcount', 'type="number" value="32" min="1" max="128"')}</label>${button('history', 'Read', 'data-stopped', 'history')}`) + out('history'))}
      ${card('Program entry and exception stops', row(`<label>Program name ${input('program', 'placeholder="myprogram"')}</label>
          ${button('arm-process', 'Arm entry stop', 'data-stopped', 'process-break')}${button('clear-process', 'Clear entry stop', 'data-stopped', 'process-break')}`) +
        `<p class="wu-note">Arm while paused, then resume and launch the program in the guest.</p>` +
        row(`<label>Exception vectors ${input('vectors', 'value="2,3,4,5,8,10,11"')}</label>${button('exceptions', 'Set stops', 'data-stopped', 'exception-mask')}
          ${button('clear-exceptions', 'Disable stops', 'data-stopped', 'exception-mask')}`) + out('stop-settings'))}
      ${card('Crash and process inspection', row(`${button('tasks', 'Tasks', 'data-stopped', 'tasks')}${button('exception', 'Last exception', 'data-stopped', 'exception')}
          <label>Process ${input('process', 'value="0" title="0 means current process"')}</label>${button('segments', 'Loaded segments', 'data-stopped', 'segments')}
          ${button('postmortem', 'Crash report')}`) + out('inspection', 'These inspections do not require the guest bridge.'))}
      ${card('Guest debug output', row(`${button('output-on', 'Capture', 'data-stopped', 'guest-output')}${button('output-off', 'Stop', 'data-stopped', 'guest-output')}
          ${button('output-read', 'Read', 'data-stopped', 'guest-output')}${button('output-clear', 'Clear', 'data-stopped', 'guest-output')}`) +
        `<p class="wu-note">Messages sent through uaelib function 86, not console or serial output.</p>` + out('output'))}
    </div>`],
    input: ['Input', `<div class="wu-grid">
      ${card('Keyboard', row(`<label>Raw key ${input('keycode', 'placeholder="0x40 (space)"')}</label>${button('key-tap', 'Tap', 'data-connected', 'input-sequence')}
          ${button('key-down', 'Press', 'data-connected', 'input')}${button('key-up', 'Release', 'data-connected', 'input')}`) +
        row(`<label>Text ${input('typetext', 'placeholder="dir" style="width:220px"')}</label>${button('type', 'Type (US keymap)', 'data-connected', 'input-sequence')}
          <label><input type="checkbox" id="wu-typeresume" checked> Resume</label>`))}
      ${card('Mouse and joystick', row(`<label>Port <select id="wu-port"><option>0</option><option>1</option></select></label>
          <label>X ${input('mousex', 'type="number" value="10" min="-127" max="127"')}</label><label>Y ${input('mousey', 'type="number" value="0" min="-127" max="127"')}</label>
          ${button('mouse', 'Move', 'data-connected', 'input')}`) +
        row(`<select id="wu-joy"><option>up</option><option>down</option><option>left</option><option>right</option><option value="button0">fire / left button</option><option value="button1">second / right button</option></select>
          ${button('joy-down', 'Press', 'data-connected', 'input')}${button('joy-up', 'Release', 'data-connected', 'input')}`))}
      ${card('Remote input', row(`${button('input-status', 'Status', 'data-connected', 'input')}${button('input-release', 'Release all', 'data-connected', 'input')}`) +
        `<p class="wu-note">Input reaches the guest through WinUAE's input handlers, also when a game has taken over the machine. A stopped guest sees it after resuming.</p>` + out('input'))}
    </div>`],
    files: ['Files &amp; disks', `<div class="wu-grid">
      ${card('Floppy drives', row(`<label>Drive <select id="wu-drive"><option>0</option><option>1</option><option>2</option><option>3</option></select></label>
          <label>Image ${input('diskfile', 'placeholder="/path/disk.adf" style="width:220px"')}</label>`) +
        row(`${button('disk-insert', 'Insert', '')}${button('disk-eject', 'Eject', '')}${button('disk-status', 'Status', '')}`) +
        `<p class="wu-note">WinUAE applies drive changes at launch, so a WinUAE started by DevBench restarts and the debugger reconnects.</p>` + out('disk'))}
      ${card('Executables and symbols', row(`<label>File ${input('exefile', 'placeholder="/path/program" style="width:260px"')}</label>`) +
        row(`${button('hunk-inspect', 'Inspect Hunk', '')}${button('symbols', 'Symbols', '')}`) +
        row(`<label>Placements ${input('placements', 'placeholder=\'[{"address":"0x40000","capacity":4096,"memory":"chip"}]\' style="width:300px"')}</label>${button('hunk-load', 'Load Hunk', 'data-stopped', 'memory-check')}`) +
        row(`<label>Raw load at ${input('rawaddr', 'placeholder="0x40000"')}</label>${button('raw-load', 'Load raw', 'data-stopped', 'memory-check')}`) + out('files'))}
      ${card('Symbols and DWARF', row(`<label>Symbol ${input('symname')}</label><label>Format <select id="wu-symformat"><option>u32</option><option>u16</option><option>u8</option><option>s32</option><option>s16</option><option>s8</option></select></label>
          ${button('symbol-read', 'Read symbol')}`) +
        row(`<label>Mappings ${input('mappings', 'placeholder=\'[{"section":".text","segment":0}]\' style="width:300px"')}</label>`) +
        row(`${button('source', 'Source line')}<label>Expression ${input('expression', 'placeholder="state->count"')}</label>${button('variable', 'Variable')}${button('backtrace', 'Backtrace')}`) +
        `<p class="wu-note">DWARF needs a linked ELF32 m68k file and DevBench's dwarf extra (pyelftools).</p>` + out('dwarf'))}
    </div>`],
  };
  panel.innerHTML = `
    <style>
      #panel-winuae { overflow:auto; }
      .wu-row { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin:8px 0; }
      .wu-grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(min(100%,420px),1fr)); gap:14px; align-items:start; }
      .wu-card { background:var(--bg-card); border:1px solid var(--border); border-radius:6px; padding:14px; min-width:0; }
      .wu-card h3 { margin:0 0 8px; font-size:14px; }
      #panel-winuae input, #panel-winuae select { color:var(--text-primary); background:var(--bg-input); border:1px solid var(--border); border-radius:4px; padding:5px; max-width:100%; }
      #panel-winuae input:not([type=checkbox]) { width:120px; }
      #panel-winuae pre, .wu-mono { overflow:auto; font:12px/1.55 monospace; max-height:380px; white-space:pre; margin:6px 0; }
      #panel-winuae button:disabled { opacity:.45; cursor:default; }
      #panel-winuae img { max-width:100%; image-rendering:pixelated; border:1px solid var(--border); }
      #wu-error { color:#ff9090; white-space:pre-wrap; }
      .wu-note { color:var(--text-secondary); font-size:12px; line-height:1.55; }
      .wu-table { width:100%; font:12px monospace; text-align:left; }
      .wu-table td, .wu-table th { padding:4px; }
      .wu-tabs { display:flex; flex-wrap:wrap; gap:4px; border-bottom:1px solid var(--border); margin:10px 0; }
      .wu-tabs div { padding:6px 12px; cursor:pointer; color:var(--text-secondary); border-bottom:2px solid transparent; }
      .wu-tabs div.active { color:var(--text-primary); border-bottom-color:#7cb7ff; }
      .wu-link { cursor:pointer; text-decoration:underline dotted; }
      .wu-note-call { color:#7cb7ff; } .wu-src-line { color:#7fdf7f; padding-left:12px; }
    </style>
    <h2>WinUAE native debugger</h2>
    <p class="wu-note">Inspect the emulator directly, including a crashed guest. Connecting pauses the CPU (WinUAE GDB attach behavior).
      Pause before reading or editing state. Disconnect releases this session's breakpoints and resumes execution.</p>
    <p id="wu-setup" class="wu-note" hidden>Enable <code>[winuae_debug] enabled = true</code> and set its GDB port in your local DevBench config, then restart DevBench.
      WinUAE needs <code>debugging_features=gdbserver</code>, a matching <code>gdb_port</code> and <code>cpu_threaded=false</code>.
      Only one debugger can use that port at a time.</p>
    <div class="wu-row">
      <strong id="wu-status" role="status">Loading configuration…</strong>
      <button class="btn" id="wu-connect" data-wu="connect">Connect &amp; pause</button>
      ${button('disconnect', 'Disconnect', 'data-connected')}
      <button class="btn" id="wu-pause" data-wu="pause" data-connected>Pause</button>
      ${button('resume', 'Resume')}${button('step', 'Step')}${button('step100', 'Step &times;100')}
      ${button('step_over', 'Step over', 'data-stopped', 'step-over')}${button('step_out', 'Step out')}
      ${button('soft-reset', 'Soft reset', 'data-connected', 'input')}${button('hard-reset', 'Hard reset', 'data-connected', 'input')}
      ${button('refresh', 'Refresh CPU')}
    </div>
    <div id="wu-error" role="alert"></div>
    <div class="wu-tabs">${Object.entries(subtabs).map(([key, [label]], i) => `<div data-wu-tab="${key}"${i ? '' : ' class="active"'}>${label}</div>`).join('')}</div>
    ${Object.entries(subtabs).map(([key, [, html]], i) => `<div class="wu-sub" data-wu-sub="${key}"${i ? ' hidden' : ''}>${html}</div>`).join('')}`;
  document.querySelector('.content').append(panel);

  const el = id => document.getElementById('wu-' + id);
  const hex = (n, width = 8) => '0x' + (Number(n) >>> 0).toString(16).padStart(width, '0');
  const json = value => typeof value === 'string' ? value : JSON.stringify(value, null, 2);
  const show = (id, value) => { el(id).textContent = json(value); };
  const escape = s => String(s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const list = id => el(id).value.split(',').map(s => s.trim()).filter(Boolean);
  const kinds = {0:'execute', 2:'write', 3:'read', 4:'access'};
  let state = {connected:false, enabled:false, capabilities:{}};
  let busy = false, memHistory = [];

  async function request(action, args = {}) {
    const response = await fetch('/api/winuae/' + action, action === 'status' ? {} : {
      method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(args)
    });
    const body = await response.json();
    if (!response.ok || !body.ok) throw new Error(body.error || 'Debugger request failed');
    return body.result;
  }
  function controls() {
    for (const b of panel.querySelectorAll('button[data-wu]')) {
      const capability = b.dataset.cap;
      b.disabled = busy ||
        (b.hasAttribute('data-connected') && !state.connected) ||
        (b.hasAttribute('data-stopped') && (!state.connected || state.execution !== 'stopped')) ||
        (capability && !(state.capabilities.commands || []).includes(capability));
    }
    el('connect').disabled = busy || !state.enabled || state.connected;
    el('pause').disabled = busy || !state.connected || state.execution === 'stopped';
    el('back').disabled = busy || !memHistory.length || state.execution !== 'stopped';
  }
  function render() {
    el('status').textContent = `${state.host || ''}:${state.port || ''} — ${state.execution}` +
      (state.last_stop ? ` (${state.last_stop})` : '') + (state.last_error ? ` — ${state.last_error}` : '');
    el('setup').hidden = state.enabled;
    const tbody = el('points').querySelector('tbody'); tbody.replaceChildren();
    for (const point of state.breakpoints || []) {
      const tr = document.createElement('tr');
      for (const value of [hex(point.address) + (point.symbol ? ` ${point.symbol}` : ''), kinds[point.kind] + (point.oneshot ? ' once' : ''),
                           point.size, point.hits, point.skip]) {
        const td = document.createElement('td'); td.textContent = value; tr.append(td);
      }
      const td = document.createElement('td'), b = document.createElement('button');
      b.className = 'btn'; b.textContent = 'Remove'; b.setAttribute('data-stopped', '');
      b.addEventListener('click', () => perform(() => request('breakpoint', {operation:'remove', address:point.address, kind:kinds[point.kind], size:point.size})));
      td.append(b); tr.append(td); tbody.append(tr);
    }
    controls();
  }
  function renderRegisters(regs) {
    const cell = name => regs[name] === undefined ? '' : `<span class="wu-link" data-reg="${name}">${name.padEnd(3)} ${hex(regs[name], name === 'SR' ? 4 : 8)}</span>`;
    const lines = Array.from({length:8}, (_, i) => `${cell('D' + i)}    ${cell('A' + i)}`);
    lines.push(`${cell('SR')}        ${cell('PC')}`, ['USP', 'ISP', 'MSP'].map(cell).filter(Boolean).join('    '));
    el('registers').innerHTML = lines.join('\n');
  }
  async function disassemble(address = el('disaddr').value) {
    const listing = await request('disassemble', {address, count:Number(el('discount').value), annotate:el('annotate').checked,
      library:el('library').value || null, source_xref:el('xref').checked, project:el('project').value || null});
    el('disassembly').innerHTML = listing.lines.map(line => escape(line.text) +
      (line.annotation ? ` <span class="wu-note-call">; ${escape(line.annotation)}</span>` : '') +
      (line.source ? `\n<span class="wu-src-line">↳ ${escape(line.source.symbol || '')} ${escape(line.source.file || '')}${line.source.line ? ':' + line.source.line : ''}</span>` : '')).join('\n');
  }
  async function cpu() {
    if (state.execution !== 'stopped' || !state.connected) return;
    renderRegisters(await request('registers'));
    if ((state.capabilities.commands || []).includes('disasm')) await disassemble();
    if ((state.capabilities.commands || []).includes('watch')) await watches();
  }
  async function watches() {
    const listing = await request('watch'), last = await request('watch', {operation:'last'}).catch(() => null);
    const lines = listing.watches.map(w => `#${w.id} ${hex(w.address)}+${w.size} access=${'RWI'.split('').filter((_, i) => w.access & (1 << i)).join('')}` +
      `${w.value !== null ? ` value=${hex(w.value)}/${hex(w.mask)}` : ''}${w.change_only ? ' changes' : ''}${w.log_only ? ' log' : ''}${w.range ? '' : ' exact'}`);
    lines.push(...listing.gdb_watchpoints.map(w => `gdb ${w.kind} ${hex(w.address)}+${w.size}`));
    if (last && last.address !== undefined)
      lines.push(`last hit: ${hex(last.address)} size=${last.size} value=${hex(last.value)} pc=${hex(last.instruction_pc)} source=${hex(last.source, 4)}`);
    el('watches').textContent = lines.join('\n') || 'No watchpoints.';
  }
  async function readMemory() {
    const data = await request('read_memory', {address:el('memaddr').value, size:Number(el('memsize').value)});
    const bytes = data.hex.match(/../g).map(b => parseInt(b, 16)), format = el('memformat').value, lines = [];
    const printable = b => b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : '.';
    for (let off = 0; off < bytes.length; off += format === 'ascii' ? 64 : 16) {
      const chunk = bytes.slice(off, off + (format === 'ascii' ? 64 : 16)), address = hex(data.address + off);
      if (format === 'bytes') lines.push(`${address}  ${chunk.map(b => b.toString(16).padStart(2, '0')).join(' ').padEnd(48)}  ${escape(chunk.map(printable).join(''))}`);
      else if (format === 'ascii') lines.push(`${address}  ${escape(chunk.map(printable).join(''))}`);
      else {
        const size = format === 'words' ? 2 : 4, parts = [];
        for (let i = 0; i + size <= chunk.length; i += size) {
          const value = chunk.slice(i, i + size).reduce((v, b) => v * 256 + b, 0);
          parts.push(size === 4 ? `<span class="wu-link" data-follow="${hex(value)}">${value.toString(16).padStart(8, '0')}</span>` : value.toString(16).padStart(4, '0'));
        }
        lines.push(`${address}  ${parts.join(' ')}`);
      }
    }
    el('memory').innerHTML = `<pre>${lines.join('\n')}</pre>`;
  }
  async function slots() {
    const data = await request('snapshots');
    const tbody = el('slots').querySelector('tbody'); tbody.replaceChildren();
    const files = [...data.slots, ...data.auto];
    for (const slot of files) {
      const tr = document.createElement('tr');
      tr.innerHTML = `<td>${escape(slot.slot)}</td><td>${slot.size ? new Date(slot.mtime * 1000).toLocaleString() : 'empty'}</td>
        <td><input value="${escape(data.labels[slot.slot] || '')}" placeholder="label"></td><td></td>`;
      tr.querySelector('input').addEventListener('change', e => perform(() => request('snapshots', {operation:'label', slot:slot.slot, label:e.target.value})));
      for (const [label, operation] of [['Save', 'save'], ['Load', 'load']]) {
        if (operation === 'save' && slot.slot.startsWith('auto')) continue;
        const b = document.createElement('button'); b.className = 'btn'; b.textContent = label;
        b.disabled = !state.connected || state.execution !== 'stopped' || (operation === 'load' && !slot.size);
        b.addEventListener('click', () => perform(async () => { show('statemsg', await request('snapshots', {operation, slot:slot.slot})); await slots(); }));
        tr.lastElementChild.append(b);
      }
      tbody.append(tr);
    }
    const options = files.filter(s => s.size).map(s => `<option>${escape(s.slot)}</option>`).join('');
    el('diffa').innerHTML = options; el('diffb').innerHTML = options;
    const ring = data.autosnap;
    el('autosnap').textContent = ring.enabled ? `every ${ring.interval}s, ring ${ring.ring_size}, ${ring.saves} saves` +
      (ring.last_error ? ` — ${ring.last_error}` : '') : 'off';
  }
  function image(id, result) {
    el(id).innerHTML = `<img alt="${id}" src="data:image/png;base64,${result.png_base64}"><div class="wu-note">${escape(result.path || `${result.width}×${result.height}`)}</div>`;
  }
  function custom(data) {
    const d = data.decoded, regs = data.registers;
    const word = name => `${name}=${hex(regs[name], 4)}`;
    el('custom').textContent = [
      `DMA        ${word('DMACONR')}  ${d.dmacon.join(' ')}`,
      `Interrupts ${word('INTENAR')}  ${d.intena.join(' ')}`,
      `Requests   ${word('INTREQR')}  ${d.intreq.join(' ')}`,
      `Display    ${word('BPLCON0')}  ${d.planes} planes ${d.bplcon0.join(' ')}`,
      `           ${['BPLCON1','BPLCON2','BPLCON3','BPL1MOD','BPL2MOD','DIWSTRT','DIWSTOP','DDFSTRT','DDFSTOP','FMODE'].map(word).join(' ')}`,
      `Bitplanes  ${d.bitplanes.slice(0, Math.max(d.planes, 1)).map(n => hex(n)).join(' ')}`,
      `Copper     COP1LC=${hex(d.cop1lc)} COP2LC=${hex(d.cop2lc)}`,
      `Sprites    ${d.sprites.map(n => hex(n)).join(' ')}`,
      `Blitter    ${word('BLTCON0')} ${word('BLTCON1')} A=${hex(d.blitter.apt)} B=${hex(d.blitter.bpt)} C=${hex(d.blitter.cpt)} D=${hex(d.blitter.dpt)}`,
      `Disk       DSKPT=${hex(d.dskpt)} ${word('DSKLEN')}`,
      `Colors     ${d.colors.map(c => c.toString(16).padStart(3, '0')).join(' ')}`,
      data.beam ? `Beam       frame ${data.beam.frame} line ${data.beam.vpos} position ${data.beam.hpos}` : '',
    ].join('\n');
  }
  const joy = () => el('joy').value.startsWith('button') ? {event:'button', button:Number(el('joy').value.slice(6))} : {event:'joystick', direction:el('joy').value};
  const mappings = () => el('mappings').value ? JSON.parse(el('mappings').value) : [];
  const watchSources = () => [...panel.querySelectorAll('.wu-src:checked')].map(c => c.value);

  const handlers = {
    connect: () => run('connect'), disconnect: () => run('disconnect'), pause: () => run('pause'), resume: () => run('resume'),
    step: () => run('step', true), step100: () => run('step', false, {count:100}),
    step_over: () => run('step_over', true),
    step_out: async () => { const r = await request('step_out'); el('error').textContent = r.returned ? '' : `Step out stopped: ${r.reason}`; },
    'soft-reset': () => run('reset', false, {hard:false}), 'hard-reset': () => run('reset', false, {hard:true}),
    refresh: () => cpu(),
    'write-register': async () => { await request('registers', {register:el('reg').value, value:el('value').value}); await cpu(); },
    disassemble: () => disassemble(),
    'add-bp': () => request('breakpoint', {operation:'add', address:el('bpaddr').value, skip:Number(el('bpskip').value), oneshot:el('oneshot').checked}),
    'add-bp-symbol': () => request('breakpoint', {operation:'add', symbol:el('bpaddr').value, skip:Number(el('bpskip').value), oneshot:el('oneshot').checked,
      project:el('project').value || null}),
    'clear-bps': () => request('breakpoint', {operation:'clear'}),
    'add-wp': async () => {
      await request('watch', {operation:'add', address:el('wpaddr').value, size:Number(el('wpsize').value), access:el('wprwi').value,
        sources:watchSources(), value:el('wpvalue').value || null, mask:el('wpmask').value || null,
        change_only:el('mustchange').checked, exact:el('exact').checked, log_only:el('logonly').checked});
      await watches();
    },
    'clear-wps': async () => { await request('watch', {operation:'clear'}); await watches(); },
    'watch-events': async () => show('watches', await request('watch', {operation:'events'})),
    'read-memory': () => readMemory(),
    'mem-back': () => { el('memaddr').value = memHistory.pop(); return readMemory(); },
    'write-memory': async () => { await request('write_memory', {address:el('memaddr').value, hex_data:el('memdata').value.replace(/\s+/g, '')}); await readMemory(); },
    'memory-map': async () => show('memmap', await request('inspect', {subject:'memory-map'})),
    stack: async () => {
      const data = await request('stack', {depth:Number(el('stackdepth').value)});
      el('stack').textContent = `SP ${hex(data.sp)}\n` + data.words.map(w => `+${String(w.offset).padStart(4)}  ${hex(w.address)}  ${hex(w.value)}  ${w.kind}`).join('\n');
    },
    search: async () => {
      const r = await request('memory_search', {address:el('findaddr').value, length:el('findlen').value, hex:el('findhex').value});
      el('found').textContent = (r.matches.map(m => hex(m)).join('\n') || 'No matches.') + (r.limit_reached ? `\n… more from ${hex(r.next_address)}` : '');
    },
    custom: async () => custom(await request('custom')),
    copper: async () => {
      const r = await request('copper', {address:el('copaddr').value, count:Number(el('copcount').value)});
      el('copper').textContent = r.instructions.map(i => `${hex(i.address)}  ${i.raw}  ${i.text}`).join('\n');
    },
    screenshot: async () => image('screen', await request('screenshot')),
    bitmap: async () => image('bitmap', await request('bitmap', {width:Number(el('bmw').value), height:Number(el('bmh').value),
      planes:list('bmplanes'), palette:list('bmpalette'), row_stride:el('bmstride').value ? Number(el('bmstride').value) : null})),
    'state-save': async () => { show('statemsg', await request('checkpoint', {operation:'save', path:el('statepath').value})); await slots(); },
    'state-load': async () => show('statemsg', await request('checkpoint', {operation:'restore', path:el('statepath').value})),
    autosnap: async () => { await request('autosnap', {interval:Number(el('autoint').value), ring_size:Number(el('autoring').value)}); await slots(); },
    'autosnap-off': async () => { await request('autosnap', {interval:0}); await slots(); },
    diff: async () => {
      const r = await request('snapshot_diff', {a:el('diffa').value, b:el('diffb').value});
      el('diff').textContent = [`${r.unchanged} chunks unchanged`, ...r.changed.map(c => `${c.chunk}: ${c.differing_bytes} bytes differ` +
        (c.size_a !== c.size_b ? ` (size ${c.size_a} → ${c.size_b})` : '') + c.regions.map(g => `\n  +${hex(g.start)}..${hex(g.end)} ${g.bytes} bytes`).join('')),
        ...r.only_in_a.map(k => `only in A: ${k}`), ...r.only_in_b.map(k => `only in B: ${k}`)].join('\n');
    },
    lookup: async () => show('looked', await request('lookup', {address:el('lookaddr').value})),
    'fd-lookup': async () => show('fd', await request('fd', {operation:'lookup', offset:el('fdoffset').value, library:el('fdlib').value})),
    'fd-libraries': async () => show('fd', await request('fd', {operation:'libraries'})),
    'fd-load': async () => show('fd', await request('fd', {operation:'load', path:el('fdpath').value, library:el('fdlib').value})),
    'dma-add': async () => show('dma', await request('dma_watch', {operation:'add', address:el('dmaaddr').value, length:Number(el('dmalen').value),
      mode:el('dmamode').value, sources:[...panel.querySelectorAll('.wu-dmasrc:checked')].map(c => c.value)})),
    'dma-list': async () => show('dma', await request('dma_watch')),
    'dma-remove': async () => show('dma', await request('dma_watch', {operation:'remove', id:Number(el('dmaid').value)})),
    'cond-add': async () => show('conditions', await request('condition', {operation:'add', register:el('condreg').value, operator:el('condop').value,
      value:el('condvalue').value, signed:el('condsigned').checked})),
    'cond-list': async () => show('conditions', await request('condition')),
    'cond-remove': async () => { await request('condition', {operation:'remove', id:Number(el('condid').value)}); show('conditions', await request('condition')); },
    'history-on': () => request('history', {operation:'on', count:1}), 'history-off': () => request('history', {operation:'off', count:1}),
    history: async () => show('history', await request('history', {count:Number(el('histcount').value)})),
    'arm-process': async () => { await request('process_break', {operation:'name', name:el('program').value}); show('stop-settings', await request('process_break')); },
    'clear-process': async () => { await request('process_break', {operation:'clear'}); show('stop-settings', await request('process_break')); },
    exceptions: async () => show('stop-settings', await request('exception_mask', {vectors:list('vectors').map(Number)})),
    'clear-exceptions': async () => show('stop-settings', await request('exception_mask', {vectors:[]})),
    tasks: async () => show('inspection', await request('inspect', {subject:'tasks'})),
    exception: async () => show('inspection', await request('inspect', {subject:'exception'})),
    segments: async () => show('inspection', await request('inspect', {subject:'segments', process:el('process').value})),
    postmortem: async () => show('inspection', await request('postmortem')),
    'output-on': () => request('guest_output', {operation:'on'}), 'output-off': () => request('guest_output', {operation:'off'}),
    'output-read': async () => show('output', await request('guest_output')), 'output-clear': async () => show('output', await request('guest_output', {operation:'clear'})),
    'key-tap': async () => show('input', await request('key_tap', {codes:[el('keycode').value], resume:true})),
    'key-down': async () => show('input', await request('input', {event:'key', code:el('keycode').value, pressed:true})),
    'key-up': async () => show('input', await request('input', {event:'key', code:el('keycode').value, pressed:false})),
    type: async () => show('input', await request('type_text', {text:el('typetext').value, layout:'us', resume:el('typeresume').checked})),
    mouse: async () => {
      const port = Number(el('port').value);
      for (const [axis, delta] of [['x', Number(el('mousex').value)], ['y', Number(el('mousey').value)]])
        if (delta) await request('input', {event:'mouse', port, axis, delta});
      show('input', await request('input'));
    },
    'joy-down': async () => show('input', await request('input', {...joy(), port:Number(el('port').value), pressed:true})),
    'joy-up': async () => show('input', await request('input', {...joy(), port:Number(el('port').value), pressed:false})),
    'input-status': async () => show('input', await request('input')),
    'input-release': async () => show('input', await request('input', {event:'release'})),
    'disk-insert': async () => show('disk', await request('disk', {operation:'insert', drive:Number(el('drive').value), file:el('diskfile').value})),
    'disk-eject': async () => show('disk', await request('disk', {operation:'eject', drive:Number(el('drive').value)})),
    'disk-status': async () => show('disk', await request('disk')),
    'hunk-inspect': async () => show('files', await request('hunk_inspect', {file:el('exefile').value})),
    symbols: async () => show('files', await request('symbols', {file:el('exefile').value})),
    'hunk-load': async () => show('files', await request('hunk_load', {file:el('exefile').value, placements:JSON.parse(el('placements').value || '[]')})),
    'raw-load': async () => show('files', await request('load', {file:el('exefile').value, address:el('rawaddr').value})),
    'symbol-read': async () => show('dwarf', await request('symbol_read', {file:el('exefile').value, symbol:el('symname').value,
      format:el('symformat').value, mappings:el('mappings').value ? mappings() : null})),
    source: async () => show('dwarf', await request('source', {file:el('exefile').value, mappings:mappings()})),
    variable: async () => show('dwarf', await request('variable', {file:el('exefile').value, mappings:mappings(), expression:el('expression').value})),
    backtrace: async () => show('dwarf', await request('backtrace', {file:el('exefile').value, mappings:mappings()})),
  };
  async function run(action, waitForStop = false, args = {}) {
    state = await request(action, args); render();
    if (waitForStop) state = await request('wait', {timeout:5});
    if (state.execution === 'stopped') await cpu();
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
  for (const name of [...Array.from({length:8}, (_, i) => 'D' + i), ...Array.from({length:8}, (_, i) => 'A' + i), 'SR', 'PC', 'USP', 'ISP', 'MSP']) {
    const option = document.createElement('option'); option.textContent = name; el('reg').append(option);
  }
  panel.addEventListener('click', event => {
    const sub = event.target.closest('[data-wu-tab]');
    if (sub) {
      for (const t of panel.querySelectorAll('[data-wu-tab]')) t.classList.toggle('active', t === sub);
      for (const p of panel.querySelectorAll('[data-wu-sub]')) p.hidden = p.dataset.wuSub !== sub.dataset.wuTab;
      if (sub.dataset.wuTab === 'state') perform(slots);
      return;
    }
    const reg = event.target.closest('[data-reg]');
    if (reg) { el('reg').value = reg.dataset.reg; el('value').value = reg.textContent.trim().split(/\s+/).pop(); el('value').focus(); return; }
    const follow = event.target.closest('[data-follow]');
    if (follow) { memHistory.push(el('memaddr').value); el('memaddr').value = follow.dataset.follow; perform(readMemory); return; }
    const action = event.target.closest('[data-wu]')?.dataset.wu;
    if (action && handlers[action]) perform(handlers[action]);
  });
  el('memformat').addEventListener('change', () => { if (state.execution === 'stopped') perform(readMemory); });
  tab.addEventListener('click', () => perform(async () => {}));
  setInterval(() => {
    const snapshots = !panel.querySelector('[data-wu-sub="state"]').hidden;
    if (!document.hidden && panel.classList.contains('active') && !busy) perform(snapshots ? slots : async () => {}, false);
  }, 1500);
  perform(async () => {});
})();
