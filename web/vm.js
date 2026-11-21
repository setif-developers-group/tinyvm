/* TinyVM - JavaScript port of the VM and assembler (browser + Node.js).
 * Behaviour must match src/c/vm.c and tools/asm.py; see docs/ISA.md. */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.TinyVM = factory();
}(typeof self !== 'undefined' ? self : this, function () {
  'use strict';

  const MEM_SIZE = 65536;
  const ST = { OK: 0, HALTED: 1, ERR_OPCODE: 2, ERR_DIV_ZERO: 3, ERR_MEM: 4 };
  const ST_TEXT = ['running', 'halted', 'fault: illegal opcode', 'fault: division by zero',
                   'fault: memory access out of range'];
  const FLAG = { Z: 1, N: 2, C: 4, V: 8 };

  // [mnemonic, opcode, operand format]
  const OPS = [
    ['NOP', 0x00, 'none'], ['HALT', 0x01, 'none'], ['MOV', 0x02, 'rr'], ['LDI', 0x03, 'ri'],
    ['LD', 0x05, 'mem'], ['ST', 0x06, 'mem'], ['LDB', 0x07, 'mem'], ['STB', 0x08, 'mem'],
    ['ADD', 0x10, 'rrr'], ['SUB', 0x11, 'rrr'], ['MUL', 0x12, 'rrr'], ['DIV', 0x13, 'rrr'],
    ['MOD', 0x14, 'rrr'], ['AND', 0x15, 'rrr'], ['OR', 0x16, 'rrr'], ['XOR', 0x17, 'rrr'],
    ['SHL', 0x18, 'rrr'], ['SHR', 0x19, 'rrr'], ['ADDI', 0x1A, 'rri'], ['NOT', 0x1B, 'rr'],
    ['CMP', 0x20, 'cmp'], ['CMPI', 0x21, 'cmpi'],
    ['JMP', 0x30, 'addr'], ['JZ', 0x31, 'addr'], ['JNZ', 0x32, 'addr'], ['JLT', 0x33, 'addr'],
    ['JGT', 0x34, 'addr'], ['JLE', 0x35, 'addr'], ['JGE', 0x36, 'addr'],
    ['CALL', 0x40, 'addr'], ['RET', 0x41, 'none'], ['PUSH', 0x42, 'r'], ['POP', 0x43, 'r'],
    ['OUT', 0x50, 'r'], ['OUTC', 0x51, 'r'],
  ];
  const BY_NAME = Object.fromEntries(OPS.map(([n, c, f]) => [n, { code: c, fmt: f }]));
  BY_NAME.JE = BY_NAME.JZ;
  BY_NAME.JNE = BY_NAME.JNZ;
  const BY_CODE = Object.fromEntries(OPS.map(([n, c, f]) => [c, { name: n, fmt: f }]));

  const regName = (i) => (i === 15 ? 'sp' : 'r' + i);

  /* ------------------------------------------------------------------ VM */
  class VM {
    constructor(onOutput) {
      this.mem = new Uint8Array(MEM_SIZE);
      this.dv = new DataView(this.mem.buffer);
      this.regs = new Uint32Array(16);
      this.onOutput = onOutput || (() => {});
      this.reset();
    }

    reset() {
      this.mem.fill(0);
      this.regs.fill(0);
      this.regs[15] = MEM_SIZE;
      this.pc = 0; this.flags = 0; this.status = ST.OK; this.cycles = 0;
    }

    load(bytes, addr = 0) {
      if (addr + bytes.length > MEM_SIZE) return false;
      this.mem.set(bytes, addr);
      return true;
    }

    _r32(a) { return a + 4 > MEM_SIZE ? null : this.dv.getUint32(a, true); }
    _w32(a, v) { if (a + 4 > MEM_SIZE) return false; this.dv.setUint32(a, v >>> 0, true); return true; }
    _fault(s) { this.status = s; return s; }

    _sub(a, b) {
      const r = (a - b) | 0;
      this.flags = (r === 0 ? FLAG.Z : 0) | (r < 0 ? FLAG.N : 0) |
                   ((a >>> 0) < (b >>> 0) ? FLAG.C : 0) | ((((a ^ b) & (a ^ r)) < 0) ? FLAG.V : 0);
    }

    step() {
      if (this.status !== ST.OK) return this.status;
      const ins = this._r32(this.pc);
      if (ins === null) return this._fault(ST.ERR_MEM);

      const op = ins >>> 24, rd = (ins >>> 20) & 15, ra = (ins >>> 16) & 15, rb = (ins >>> 12) & 15;
      const imm = ins & 0xFFFF, simm = (imm << 16) >> 16;
      const R = this.regs;
      let next = (this.pc + 4) >>> 0, v;
      const z = (this.flags & FLAG.Z) !== 0, n = (this.flags & FLAG.N) !== 0, o = (this.flags & FLAG.V) !== 0;

      switch (op) {
        case 0x00: break;
        case 0x01: this.status = ST.HALTED; break;
        case 0x02: R[rd] = R[ra]; break;
        case 0x03: R[rd] = simm; break;
        case 0x05: v = this._r32((R[ra] + simm) >>> 0); if (v === null) return this._fault(ST.ERR_MEM); R[rd] = v; break;
        case 0x06: if (!this._w32((R[ra] + simm) >>> 0, R[rd])) return this._fault(ST.ERR_MEM); break;
        case 0x07: { const a = (R[ra] + simm) >>> 0; if (a >= MEM_SIZE) return this._fault(ST.ERR_MEM); R[rd] = this.mem[a]; break; }
        case 0x08: { const a = (R[ra] + simm) >>> 0; if (a >= MEM_SIZE) return this._fault(ST.ERR_MEM); this.mem[a] = R[rd] & 0xFF; break; }
        case 0x10: R[rd] = R[ra] + R[rb]; break;
        case 0x11: R[rd] = R[ra] - R[rb]; break;
        case 0x12: R[rd] = Math.imul(R[ra], R[rb]); break;
        case 0x13: case 0x14: {
          const a = R[ra] | 0, b = R[rb] | 0;
          if (b === 0) return this._fault(ST.ERR_DIV_ZERO);
          R[rd] = op === 0x13 ? ((a / b) | 0) : ((a % b) | 0);
          break;
        }
        case 0x15: R[rd] = R[ra] & R[rb]; break;
        case 0x16: R[rd] = R[ra] | R[rb]; break;
        case 0x17: R[rd] = R[ra] ^ R[rb]; break;
        case 0x18: R[rd] = R[ra] << (R[rb] & 31); break;
        case 0x19: R[rd] = R[ra] >>> (R[rb] & 31); break;
        case 0x1A: R[rd] = R[ra] + simm; break;
        case 0x1B: R[rd] = ~R[ra]; break;
        case 0x20: this._sub(R[ra], R[rb]); break;
        case 0x21: this._sub(R[ra], simm); break;
        case 0x30: next = imm; break;
        case 0x31: if (z) next = imm; break;
        case 0x32: if (!z) next = imm; break;
        case 0x33: if (n !== o) next = imm; break;
        case 0x34: if (!z && n === o) next = imm; break;
        case 0x35: if (z || n !== o) next = imm; break;
        case 0x36: if (n === o) next = imm; break;
        case 0x40: v = (R[15] - 4) >>> 0; if (!this._w32(v, next)) return this._fault(ST.ERR_MEM); R[15] = v; next = imm; break;
        case 0x41: v = this._r32(R[15]); if (v === null) return this._fault(ST.ERR_MEM); R[15] += 4; next = v; break;
        case 0x42: v = (R[15] - 4) >>> 0; if (!this._w32(v, R[rd])) return this._fault(ST.ERR_MEM); R[15] = v; break;
        case 0x43: v = this._r32(R[15]); if (v === null) return this._fault(ST.ERR_MEM); R[15] += 4; R[rd] = v; break;
        case 0x50: this.onOutput((R[rd] | 0) + '\n'); break;
        case 0x51: this.onOutput(String.fromCharCode(R[rd] & 0xFF)); break;
        default: return this._fault(ST.ERR_OPCODE);
      }
      this.pc = next >>> 0;
      this.cycles++;
      return this.status;
    }

    run(maxCycles = 0) {
      let n = 0;
      while (this.status === ST.OK && (maxCycles === 0 || n < maxCycles)) { this.step(); n++; }
      return this.status;
    }
  }

  /* --------------------------------------------------------- disassembler */
  function disasm(ins) {
    ins >>>= 0;
    const info = BY_CODE[ins >>> 24];
    if (!info) return '.word 0x' + ins.toString(16).toUpperCase().padStart(8, '0');
    const rd = regName((ins >>> 20) & 15), ra = regName((ins >>> 16) & 15), rb = regName((ins >>> 12) & 15);
    const imm = ins & 0xFFFF, simm = (imm << 16) >> 16, name = info.name.padEnd(5);
    switch (info.fmt) {
      case 'none': return info.name;
      case 'r': return `${name} ${rd}`;
      case 'rr': return `${name} ${rd}, ${ra}`;
      case 'ri': return `${name} ${rd}, ${simm}`;
      case 'rrr': return `${name} ${rd}, ${ra}, ${rb}`;
      case 'rri': return `${name} ${rd}, ${ra}, ${simm}`;
      case 'cmp': return `${name} ${ra}, ${rb}`;
      case 'cmpi': return `${name} ${ra}, ${simm}`;
      case 'addr': return `${name} 0x${imm.toString(16).toUpperCase().padStart(4, '0')}`;
      default: return simm === 0 ? `${name} ${rd}, [${ra}]`
                                 : `${name} ${rd}, [${ra}${simm < 0 ? '-' : '+'}${Math.abs(simm)}]`;
    }
  }

  /* ------------------------------------------------------------ assembler */
  class AsmError extends Error {
    constructor(msg, line) { super(line ? `line ${line}: ${msg}` : msg); this.line = line; this.name = 'AsmError'; }
  }
  const ESC = { n: 10, t: 9, r: 13, '0': 0, '\\': 92, '"': 34, "'": 39 };

  function stripComment(line) {
    let quote = null;
    for (let i = 0; i < line.length; i++) {
      const c = line[i];
      if (quote) { if (c === '\\') i++; else if (c === quote) quote = null; }
      else if (c === '"' || c === "'") quote = c;
      else if (c === ';') return line.slice(0, i);
    }
    return line;
  }

  function parseSource(src) {
    const stmts = [];
    src.split(/\r?\n/).forEach((raw, idx) => {
      let line = stripComment(raw).trim();
      const ln = idx + 1;
      let m;
      while ((m = /^([A-Za-z_.][\w.]*)\s*:/.exec(line))) {
        stmts.push({ kind: 'label', name: m[1], line: ln });
        line = line.slice(m[0].length).trim();
      }
      if (!line) return;
      const sp = line.search(/\s/);
      const head = sp < 0 ? line : line.slice(0, sp);
      stmts.push({ kind: head[0] === '.' ? 'dir' : 'ins', name: head, rest: sp < 0 ? '' : line.slice(sp).trim(), line: ln });
    });
    return stmts;
  }

  function evalExpr(text, syms, line) {
    text = text.trim();
    if (!text) throw new AsmError('missing value', line);
    let m = /^'(\\.|[^\\])'$/.exec(text);
    if (m) {
      if (m[1][0] === '\\') { if (!(m[1][1] in ESC)) throw new AsmError('bad escape ' + m[1], line); return ESC[m[1][1]]; }
      return m[1].charCodeAt(0);
    }
    let total = 0;
    for (let term of text.match(/[+-]?\s*[^+-]+/g)) {
      term = term.trim();
      let sign = 1;
      if (term[0] === '+' || term[0] === '-') { sign = term[0] === '-' ? -1 : 1; term = term.slice(1).trim(); }
      let val;
      if (/^(0[xX][0-9a-fA-F]+|0[bB][01]+|\d+)$/.test(term)) val = Number(term);
      else if (Object.prototype.hasOwnProperty.call(syms, term)) val = syms[term];
      else throw new AsmError(`undefined symbol or bad number '${term}'`, line);
      total += sign * val;
    }
    return total;
  }

  function parseString(text, line) {
    text = text.trim();
    if (text.length < 2 || text[0] !== '"' || text[text.length - 1] !== '"') throw new AsmError('expected a double-quoted string', line);
    const body = new TextEncoder().encode(text.slice(1, -1).replace(/\\(.)/g, (_, c) => {
      if (!(c in ESC)) throw new AsmError('bad escape in string', line);
      return String.fromCharCode(ESC[c]);
    }));
    return body;
  }

  function parseReg(tok, line) {
    const t = tok.trim().toLowerCase();
    if (t === 'sp') return 15;
    const m = /^r(\d+)$/.exec(t);
    if (m && +m[1] < 16) return +m[1];
    throw new AsmError(`bad register '${tok}'`, line);
  }

  const imm16 = (v, ln) => { if (v < -32768 || v > 65535) throw new AsmError(`immediate ${v} does not fit in 16 bits`, ln); return v & 0xFFFF; };
  const addr16 = (v, ln) => { if (v < 0 || v > 0xFFFF) throw new AsmError(`address ${v} out of range`, ln); return v; };
  const word = (op, rd = 0, ra = 0, rb = 0, imm = 0) => (((op << 24) | (rd << 20) | (ra << 16) | (rb << 12) | imm) >>> 0);
  const NEED = { none: 0, r: 1, rr: 2, ri: 2, mem: 2, rrr: 3, rri: 3, cmp: 2, cmpi: 2, addr: 1 };

  function encode(s, syms) {
    const ln = s.line, name = s.name.toUpperCase();
    const info = BY_NAME[name];
    if (!info) throw new AsmError(`unknown instruction '${s.name}'`, ln);
    const args = s.rest ? s.rest.split(',').map((a) => a.trim()) : [];
    if (args.length !== NEED[info.fmt]) throw new AsmError(`${name} expects ${NEED[info.fmt]} operand(s), got ${args.length}`, ln);
    const R = (t) => parseReg(t, ln), V = (t) => evalExpr(t, syms, ln), op = info.code;
    switch (info.fmt) {
      case 'none': return word(op);
      case 'r': return word(op, R(args[0]));
      case 'rr': return word(op, R(args[0]), R(args[1]));
      case 'ri': return word(op, R(args[0]), 0, 0, imm16(V(args[1]), ln));
      case 'rrr': return word(op, R(args[0]), R(args[1]), R(args[2]));
      case 'rri': return word(op, R(args[0]), R(args[1]), 0, imm16(V(args[2]), ln));
      case 'cmp': return word(op, 0, R(args[0]), R(args[1]));
      case 'cmpi': return word(op, 0, R(args[0]), 0, imm16(V(args[1]), ln));
      case 'addr': return word(op, 0, 0, 0, addr16(V(args[0]), ln));
      default: {
        const m = /^\[\s*(\w+)\s*(?:([+-])\s*(.+?))?\s*\]$/.exec(args[1]);
        if (!m) throw new AsmError(`bad memory operand '${args[1]}'`, ln);
        const off = m[2] ? V(m[3]) * (m[2] === '-' ? -1 : 1) : 0;
        return word(op, R(args[0]), R(m[1]), 0, imm16(off, ln));
      }
    }
  }

  function layout(stmts, syms, emit) {
    const image = new Uint8Array(MEM_SIZE);
    let end = 0, pc = 0;
    const put = (addr, bytes, ln) => {
      if (addr < 0 || addr + bytes.length > MEM_SIZE) throw new AsmError('program exceeds 64 KiB memory', ln);
      image.set(bytes, addr);
      end = Math.max(end, addr + bytes.length);
    };
    const le = (v, size) => Array.from({ length: size }, (_, i) => Math.floor(v / 2 ** (8 * i)) & 0xFF);

    for (const s of stmts) {
      const ln = s.line;
      if (s.kind === 'label') {
        if (!emit) {
          if (s.name in syms) throw new AsmError(`duplicate symbol '${s.name}'`, ln);
          syms[s.name] = pc;
        }
      } else if (s.kind === 'dir') {
        const d = s.name.toLowerCase();
        if (d === '.org') pc = evalExpr(s.rest, syms, ln);
        else if (d === '.equ') {
          const i = s.rest.indexOf(','), name = (i < 0 ? s.rest : s.rest.slice(0, i)).trim();
          if (!emit) {
            if (name in syms) throw new AsmError(`duplicate symbol '${name}'`, ln);
            syms[name] = evalExpr(i < 0 ? '' : s.rest.slice(i + 1), syms, ln);
          }
        } else if (d === '.align') {
          const n = evalExpr(s.rest, syms, ln);
          if (n <= 0) throw new AsmError('.align needs a positive value', ln);
          pc += (n - (pc % n)) % n;
        } else if (d === '.space') pc += evalExpr(s.rest, syms, ln);
        else if (d === '.word' || d === '.byte') {
          const size = d === '.word' ? 4 : 1;
          for (const item of s.rest.split(',')) {
            if (emit) put(pc, le(((evalExpr(item, syms, ln) % 2 ** (8 * size)) + 2 ** (8 * size)) % 2 ** (8 * size), size), ln);
            pc += size;
          }
        } else if (d === '.string' || d === '.asciz') {
          const bytes = Array.from(parseString(s.rest, ln)).concat([0]);
          if (emit) put(pc, bytes, ln);
          pc += bytes.length;
        } else throw new AsmError(`unknown directive '${s.name}'`, ln);
      } else {
        if (pc % 4) throw new AsmError('instruction at unaligned address (use .align 4)', ln);
        if (emit) put(pc, le(encode(s, syms), 4), ln);
        pc += 4;
      }
    }
    return image.slice(0, end);
  }

  function assemble(source) {
    const stmts = parseSource(source), syms = {};
    layout(stmts, syms, false);
    return layout(stmts, syms, true);
  }

  return { VM, assemble, disasm, AsmError, MEM_SIZE, ST, ST_TEXT, FLAG, regName };
}));
