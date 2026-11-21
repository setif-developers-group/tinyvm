#!/usr/bin/env python3
"""TinyVM assembler: turns .asm source into a flat binary image loaded at address 0.

Usage:  asm.py program.asm [-o program.bin] [-l]

Syntax summary (full spec in docs/ISA.md):
    label:  MNEMONIC operands   ; comment
    .org ADDR | .equ NAME, VALUE | .word a,b | .byte a,b | .string "txt"
    .space N  | .align N
"""
import argparse
import re
import sys

MEM_SIZE = 65536

# mnemonic -> (opcode, operand format)
OPS = {
    "NOP": (0x00, "none"), "HALT": (0x01, "none"),
    "MOV": (0x02, "rr"), "LDI": (0x03, "ri"),
    "LD": (0x05, "mem"), "ST": (0x06, "mem"), "LDB": (0x07, "mem"), "STB": (0x08, "mem"),
    "ADD": (0x10, "rrr"), "SUB": (0x11, "rrr"), "MUL": (0x12, "rrr"), "DIV": (0x13, "rrr"),
    "MOD": (0x14, "rrr"), "AND": (0x15, "rrr"), "OR": (0x16, "rrr"), "XOR": (0x17, "rrr"),
    "SHL": (0x18, "rrr"), "SHR": (0x19, "rrr"), "ADDI": (0x1A, "rri"), "NOT": (0x1B, "rr"),
    "CMP": (0x20, "cmp"), "CMPI": (0x21, "cmpi"),
    "JMP": (0x30, "addr"), "JZ": (0x31, "addr"), "JE": (0x31, "addr"),
    "JNZ": (0x32, "addr"), "JNE": (0x32, "addr"),
    "JLT": (0x33, "addr"), "JGT": (0x34, "addr"), "JLE": (0x35, "addr"), "JGE": (0x36, "addr"),
    "CALL": (0x40, "addr"), "RET": (0x41, "none"),
    "PUSH": (0x42, "r"), "POP": (0x43, "r"),
    "OUT": (0x50, "r"), "OUTC": (0x51, "r"),
}

ESCAPES = {"n": 10, "t": 9, "r": 13, "0": 0, "\\": 92, '"': 34, "'": 39}


class AsmError(Exception):
    def __init__(self, msg, line=None):
        super().__init__(f"line {line}: {msg}" if line else msg)
        self.line = line


def strip_comment(line):
    quote = None
    i = 0
    while i < len(line):
        c = line[i]
        if quote:
            if c == "\\":
                i += 1
            elif c == quote:
                quote = None
        elif c in "\"'":
            quote = c
        elif c == ";":
            return line[:i]
        i += 1
    return line


def parse_source(src):
    stmts = []
    for ln, raw in enumerate(src.splitlines(), 1):
        line = strip_comment(raw).strip()
        while True:
            m = re.match(r"^([A-Za-z_.][\w.]*)\s*:", line)
            if not m:
                break
            stmts.append({"kind": "label", "name": m.group(1), "line": ln})
            line = line[m.end():].strip()
        if not line:
            continue
        parts = line.split(None, 1)
        head, rest = parts[0], (parts[1].strip() if len(parts) > 1 else "")
        stmts.append({"kind": "dir" if head.startswith(".") else "ins",
                      "name": head, "rest": rest, "line": ln})
    return stmts


def char_value(text, line):
    m = re.fullmatch(r"'(\\.|[^\\])'", text)
    if not m:
        return None
    body = m.group(1)
    if body.startswith("\\"):
        if body[1] not in ESCAPES:
            raise AsmError(f"bad escape {body}", line)
        return ESCAPES[body[1]]
    return ord(body)


def eval_expr(text, syms, line):
    text = text.strip()
    if not text:
        raise AsmError("missing value", line)
    cv = char_value(text, line)
    if cv is not None:
        return cv
    total = 0
    for term in re.findall(r"[+-]?\s*[^+-]+", text):
        term = term.strip()
        sign = 1
        if term[0] in "+-":
            sign = -1 if term[0] == "-" else 1
            term = term[1:].strip()
        if re.fullmatch(r"0[xX][0-9a-fA-F]+|0[bB][01]+|\d+", term):
            val = int(term, 0)
        elif term in syms:
            val = syms[term]
        else:
            raise AsmError(f"undefined symbol or bad number '{term}'", line)
        total += sign * val
    return total


def parse_string(text, line):
    text = text.strip()
    if len(text) < 2 or text[0] != '"' or text[-1] != '"':
        raise AsmError("expected a double-quoted string", line)
    out, i, body = bytearray(), 0, text[1:-1]
    while i < len(body):
        c = body[i]
        if c == "\\":
            i += 1
            if i >= len(body) or body[i] not in ESCAPES:
                raise AsmError("bad escape in string", line)
            out.append(ESCAPES[body[i]])
        else:
            out.extend(c.encode("utf-8"))
        i += 1
    return bytes(out)


def parse_reg(tok, line):
    t = tok.strip().lower()
    if t == "sp":
        return 15
    m = re.fullmatch(r"r(\d+)", t)
    if m and int(m.group(1)) < 16:
        return int(m.group(1))
    raise AsmError(f"bad register '{tok}'", line)


def imm16(value, line):
    if not -32768 <= value <= 65535:
        raise AsmError(f"immediate {value} does not fit in 16 bits", line)
    return value & 0xFFFF


def addr16(value, line):
    if not 0 <= value <= 0xFFFF:
        raise AsmError(f"address {value} out of range", line)
    return value


def word(op, rd=0, ra=0, rb=0, imm=0):
    return (op << 24) | (rd << 20) | (ra << 16) | (rb << 12) | imm


def encode(stmt, syms):
    ln, name = stmt["line"], stmt["name"].upper()
    if name not in OPS:
        raise AsmError(f"unknown instruction '{stmt['name']}'", ln)
    op, fmt = OPS[name]
    args = [a.strip() for a in stmt["rest"].split(",")] if stmt["rest"] else []
    need = {"none": 0, "r": 1, "rr": 2, "ri": 2, "mem": 2, "rrr": 3, "rri": 3,
            "cmp": 2, "cmpi": 2, "addr": 1}[fmt]
    if len(args) != need:
        raise AsmError(f"{name} expects {need} operand(s), got {len(args)}", ln)
    R = lambda t: parse_reg(t, ln)
    V = lambda t: eval_expr(t, syms, ln)
    if fmt == "none":
        return word(op)
    if fmt == "r":
        return word(op, rd=R(args[0]))
    if fmt == "rr":
        return word(op, rd=R(args[0]), ra=R(args[1]))
    if fmt == "ri":
        return word(op, rd=R(args[0]), imm=imm16(V(args[1]), ln))
    if fmt == "rrr":
        return word(op, rd=R(args[0]), ra=R(args[1]), rb=R(args[2]))
    if fmt == "rri":
        return word(op, rd=R(args[0]), ra=R(args[1]), imm=imm16(V(args[2]), ln))
    if fmt == "cmp":
        return word(op, ra=R(args[0]), rb=R(args[1]))
    if fmt == "cmpi":
        return word(op, ra=R(args[0]), imm=imm16(V(args[1]), ln))
    if fmt == "addr":
        return word(op, imm=addr16(V(args[0]), ln))
    # memory operand: rd, [ra(+|-)expr]
    m = re.fullmatch(r"\[\s*(\w+)\s*(?:([+-])\s*(.+?))?\s*\]", args[1])
    if not m:
        raise AsmError(f"bad memory operand '{args[1]}'", ln)
    off = 0
    if m.group(2):
        off = V(m.group(3)) * (-1 if m.group(2) == "-" else 1)
    return word(op, rd=R(args[0]), ra=R(m.group(1)), imm=imm16(off, ln))


def layout(stmts, syms, emit, listing=None):
    """Walk the program. Pass 1 (emit=False) builds symbols; pass 2 emits bytes."""
    image, end, pc = bytearray(MEM_SIZE), 0, 0

    def put(addr, data, ln):
        nonlocal end
        if addr < 0 or addr + len(data) > MEM_SIZE:
            raise AsmError("program exceeds 64 KiB memory", ln)
        image[addr:addr + len(data)] = data
        end = max(end, addr + len(data))

    for s in stmts:
        ln = s["line"]
        if s["kind"] == "label":
            if not emit:
                if s["name"] in syms:
                    raise AsmError(f"duplicate symbol '{s['name']}'", ln)
                syms[s["name"]] = pc
            continue
        start = pc
        if s["kind"] == "dir":
            d, rest = s["name"].lower(), s["rest"]
            if d == ".org":
                pc = eval_expr(rest, syms, ln)
            elif d == ".equ":
                name, _, val = rest.partition(",")
                if not emit:
                    if name.strip() in syms:
                        raise AsmError(f"duplicate symbol '{name.strip()}'", ln)
                    syms[name.strip()] = eval_expr(val, syms, ln)
            elif d == ".align":
                n = eval_expr(rest, syms, ln)
                if n <= 0:
                    raise AsmError(".align needs a positive value", ln)
                pc += (-pc) % n
            elif d == ".space":
                pc += eval_expr(rest, syms, ln)
            elif d in (".word", ".byte"):
                size = 4 if d == ".word" else 1
                for item in rest.split(","):
                    if emit:
                        v = eval_expr(item, syms, ln) & ((1 << (8 * size)) - 1)
                        put(pc, v.to_bytes(size, "little"), ln)
                    pc += size
            elif d in (".string", ".asciz"):
                data = parse_string(rest, ln) + b"\0"
                if emit:
                    put(pc, data, ln)
                pc += len(data)
            else:
                raise AsmError(f"unknown directive '{s['name']}'", ln)
        else:
            if pc % 4:
                raise AsmError("instruction at unaligned address (use .align 4)", ln)
            if emit:
                put(pc, encode(s, syms).to_bytes(4, "little"), ln)
            pc += 4
        if emit and listing is not None and s["kind"] != "label" and pc > start:
            listing.append((start, bytes(image[start:min(pc, start + 8)]), s))
    return bytes(image[:end])


def assemble(source, listing=None):
    """Assemble source text and return the binary image (bytes)."""
    stmts, syms = parse_source(source), {}
    layout(stmts, syms, emit=False)
    return layout(stmts, syms, emit=True, listing=listing)


def main(argv=None):
    ap = argparse.ArgumentParser(description="TinyVM assembler")
    ap.add_argument("source")
    ap.add_argument("-o", "--output", help="output .bin (default: source with .bin extension)")
    ap.add_argument("-l", "--listing", action="store_true", help="print an address/bytes listing")
    args = ap.parse_args(argv)
    try:
        with open(args.source, encoding="utf-8") as f:
            listing = [] if args.listing else None
            image = assemble(f.read(), listing)
    except (AsmError, OSError) as e:
        print(f"asm: error: {e}", file=sys.stderr)
        return 1
    out = args.output or re.sub(r"\.\w+$", "", args.source) + ".bin"
    with open(out, "wb") as f:
        f.write(image)
    if listing:
        for addr, data, s in listing:
            text = f"{s['name']} {s['rest']}".strip()
            print(f"{addr:04X}  {data.hex(' '):<24} {text}")
    print(f"asm: wrote {len(image)} bytes to {out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
