// Run with:  node web/test.js
// Executes every examples/*.asm in the JS VM and compares with examples/*.out.
const fs = require('fs');
const path = require('path');
const assert = require('assert');
const { VM, assemble, ST } = require('./vm.js');

const dir = path.join(__dirname, '..', 'examples');
let failed = 0, passed = 0;

function check(name, fn) {
  try { fn(); passed++; console.log('  ok   ' + name); }
  catch (e) { failed++; console.log('  FAIL ' + name + '\n       ' + e.message); }
}

for (const f of fs.readdirSync(dir).filter((x) => x.endsWith('.asm')).sort()) {
  const out = path.join(dir, f.replace(/\.asm$/, '.out'));
  if (!fs.existsSync(out)) continue;
  check('example ' + f, () => {
    let text = '';
    const vm = new VM((s) => { text += s; });
    vm.load(assemble(fs.readFileSync(path.join(dir, f), 'utf8')));
    vm.run(1e6);
    assert.strictEqual(vm.status, ST.HALTED);
    assert.strictEqual(text, fs.readFileSync(out, 'utf8'));
  });
}

check('signed division truncates toward zero', () => {
  const vm = new VM(); vm.load(assemble('LDI r1,-7\nLDI r2,2\nDIV r3,r1,r2\nMOD r4,r1,r2\nHALT')); vm.run();
  assert.strictEqual(vm.regs[3] | 0, -3); assert.strictEqual(vm.regs[4] | 0, -1);
});
check('INT_MIN / -1 does not trap', () => {
  const vm = new VM(); vm.load(assemble('DIV r3,r1,r2\nMOD r4,r1,r2\nHALT'));
  vm.regs[1] = 0x80000000; vm.regs[2] = 0xFFFFFFFF; vm.run();
  assert.strictEqual(vm.status, ST.HALTED); assert.strictEqual(vm.regs[3], 0x80000000); assert.strictEqual(vm.regs[4], 0);
});
check('MUL wraps to 32 bits', () => {
  const vm = new VM(); vm.load(assemble('MUL r3,r1,r2\nHALT')); vm.regs[1] = 0xFFFFFFFF; vm.regs[2] = 0xFFFFFFFF; vm.run();
  assert.strictEqual(vm.regs[3], 1);
});
check('signed conditional jumps', () => {
  const vm = new VM(); vm.load(assemble('CMP r1,r2\nJLT t\nLDI r3,0\nHALT\nt: LDI r3,1\nHALT'));
  vm.regs[1] = 0x80000000; vm.regs[2] = 1; vm.run();
  assert.strictEqual(vm.regs[3], 1);
});
check('divide by zero faults', () => {
  const vm = new VM(); vm.load(assemble('DIV r1,r1,r2')); vm.run();
  assert.strictEqual(vm.status, ST.ERR_DIV_ZERO);
});
check('assembler rejects bad input', () => {
  for (const s of ['FOO r1', 'LDI r16,1', 'LDI r1,70000', 'JMP nowhere', 'x: NOP\nx: NOP', '.byte 1\nNOP'])
    assert.throws(() => assemble(s), /./, s);
});
check('byte/word layout matches the C assembler', () => {
  const img = assemble('.word 1, -1\n.byte 7, \'A\'\n.string "hi\\n"');
  assert.deepStrictEqual(Array.from(img), [1,0,0,0,255,255,255,255,7,65,104,105,10,0]);
  assert.deepStrictEqual(Array.from(assemble('LDI r3, 0x1234')), [0x34, 0x12, 0x30, 0x03]);
});

console.log(`\n${passed} passed, ${failed} failed`);
process.exit(failed ? 1 : 0);
