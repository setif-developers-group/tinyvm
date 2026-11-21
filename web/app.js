/* Browser UI for TinyVM. Depends on vm.js (TinyVM) and examples.js. */
(function () {
  'use strict';
  const { VM, assemble, disasm, ST, ST_TEXT, FLAG, regName } = window.TinyVM;
  const $ = (id) => document.getElementById(id);
  const hex = (v, w) => '0x' + (v >>> 0).toString(16).toUpperCase().padStart(w, '0');

  let image = null, running = false, prev = new Uint32Array(16), trace = [];
  const vm = new VM((s) => { $('output').textContent += s; });

  // ---- setup
  const regsEl = $('regs');
  for (let i = 0; i < 16; i++) regsEl.insertAdjacentHTML('beforeend', `<div class="reg" id="r${i}"><small>${regName(i)}</small><span></span></div>`);
  const examples = window.TINYVM_EXAMPLES || {};
  for (const name of Object.keys(examples)) $('examples').add(new Option(name + '.asm', name));
  const loadExample = () => { $('source').value = examples[$('examples').value] || ''; };
  $('examples').addEventListener('change', loadExample);
  loadExample();

  function say(text, isError) { const m = $('message'); m.textContent = text; m.className = isError ? 'error' : ''; }
  function enable(on) { for (const id of ['step', 'run', 'reset']) $(id).disabled = !on; }

  function render() {
    for (let i = 0; i < 16; i++) {
      const el = $('r' + i);
      el.lastChild.textContent = `${hex(vm.regs[i], 8)} (${vm.regs[i] | 0})`;
      el.classList.toggle('changed', vm.regs[i] !== prev[i]);
    }
    prev = Uint32Array.from(vm.regs);
    $('pc').textContent = hex(vm.pc, 4);
    $('cycles').textContent = vm.cycles;
    $('flags').textContent = (vm.flags & FLAG.Z ? 'Z' : '-') + (vm.flags & FLAG.N ? 'N' : '-') + (vm.flags & FLAG.C ? 'C' : '-') + (vm.flags & FLAG.V ? 'V' : '-');
    $('status').textContent = ST_TEXT[vm.status];
    renderMem();
    $('trace').textContent = trace.join('\n');
    $('trace').scrollTop = $('trace').scrollHeight;
  }

  function renderMem() {
    let base = parseInt($('memaddr').value, 0);
    base = Number.isNaN(base) ? 0 : Math.max(0, Math.min(base & ~15, vm.mem.length - 256));
    const rows = [];
    for (let r = 0; r < 16; r++) {
      const a = base + r * 16, bytes = Array.from(vm.mem.slice(a, a + 16));
      rows.push(`${hex(a, 4)}  ${bytes.map((b) => b.toString(16).padStart(2, '0')).join(' ')}  |${bytes.map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : '.')).join('')}|`);
    }
    $('mem').textContent = rows.join('\n');
  }

  function stepOnce() {
    if (vm.status !== ST.OK) return;
    const pc = vm.pc;
    const text = vm.mem.length - pc >= 4 ? disasm(vm.dv.getUint32(pc, true)) : '<out of range>';
    vm.step();
    trace.push(`${String(vm.cycles).padStart(6)}  ${hex(pc, 4)}: ${text}`);
    if (trace.length > 300) trace.splice(0, trace.length - 300);
  }

  function load() {
    $('output').textContent = '';
    trace = [];
    vm.reset();
    vm.load(image);
    prev = new Uint32Array(16);
    render();
  }

  // ---- events
  $('assemble').addEventListener('click', () => {
    running = false; $('run').textContent = 'Run';
    try {
      image = assemble($('source').value);
      load();
      enable(true);
      say(`Assembled ${image.length} bytes.`);
    } catch (e) {
      enable(false);
      say(e.message, true);
    }
  });

  $('step').addEventListener('click', () => { stepOnce(); render(); });
  $('reset').addEventListener('click', () => { running = false; $('run').textContent = 'Run'; load(); });
  $('memaddr').addEventListener('change', renderMem);

  $('run').addEventListener('click', () => {
    if (running) { running = false; $('run').textContent = 'Run'; return; }
    running = true; $('run').textContent = 'Pause';
    const tick = () => {
      if (!running) return;
      for (let i = 0; i < 20000 && vm.status === ST.OK; i++) stepOnce();
      render();
      if (vm.status === ST.OK) requestAnimationFrame(tick);
      else { running = false; $('run').textContent = 'Run'; say(ST_TEXT[vm.status]); }
    };
    tick();
  });

  $('assemble').click();
})();
