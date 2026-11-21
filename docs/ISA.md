# TinyVM Instruction Set Architecture (v1.0)

This document is the **normative specification**. The C engine (`src/c/vm.c`) is the reference
implementation; every other implementation (JavaScript, future ports) must behave identically.

## Machine model

| Item | Definition |
|------|------------|
| Word size | 32 bits, two's complement |
| Registers | `r0`–`r15`, 32-bit. `r15` is also called `sp` (stack pointer). All are general purpose |
| Program counter | `pc`, byte address, 16-bit range (0–0xFFFF) |
| Memory | 65,536 bytes, flat, byte-addressed, **little-endian**, unaligned access allowed |
| Flags | `Z` zero, `N` negative, `C` unsigned borrow, `V` signed overflow — set **only** by `CMP`/`CMPI` |
| Reset state | all registers 0 except `sp = 0x10000`; `pc = 0`; memory zeroed; flags clear |
| Stack | full-descending: `PUSH` does `sp -= 4` then stores; `POP` loads then `sp += 4` |

Programs are loaded as a flat binary image at address 0 and execution starts at `pc = 0`.

## Instruction encoding

Every instruction is one 32-bit word, stored little-endian (so the opcode is the byte at `pc+3`).

```
 31      24 23    20 19    16 15    12 11            0
+----------+--------+--------+--------+---------------+
|  opcode  |   rd   |   ra   |   rb   |    (unused)   |   register form
+----------+--------+--------+--------+---------------+
|  opcode  |   rd   |   ra   |           imm16         |   immediate form
+----------+--------+--------+-------------------------+
```

`imm16` is sign-extended to 32 bits unless used as an address (jumps/calls: unsigned 0–0xFFFF).
The assembler accepts immediates in −32768…65535.

## Instruction reference

| Opcode | Mnemonic | Operands | Effect |
|-------:|----------|----------|--------|
| 0x00 | `NOP` | | no operation |
| 0x01 | `HALT` | | stop the machine (`status = halted`) |
| 0x02 | `MOV` | `rd, ra` | `rd = ra` |
| 0x03 | `LDI` | `rd, imm` | `rd = sign_extend(imm16)` |
| 0x05 | `LD` | `rd, [ra+imm]` | `rd = mem32[ra + simm]` |
| 0x06 | `ST` | `rd, [ra+imm]` | `mem32[ra + simm] = rd` |
| 0x07 | `LDB` | `rd, [ra+imm]` | `rd = mem8[ra + simm]` (zero-extended) |
| 0x08 | `STB` | `rd, [ra+imm]` | `mem8[ra + simm] = rd & 0xFF` |
| 0x10 | `ADD` | `rd, ra, rb` | `rd = ra + rb` (wraps) |
| 0x11 | `SUB` | `rd, ra, rb` | `rd = ra - rb` (wraps) |
| 0x12 | `MUL` | `rd, ra, rb` | `rd = low 32 bits of ra * rb` |
| 0x13 | `DIV` | `rd, ra, rb` | signed division, truncates toward zero. `rb == 0` → fault. `INT_MIN / -1 = INT_MIN` |
| 0x14 | `MOD` | `rd, ra, rb` | signed remainder (sign of dividend). `rb == 0` → fault. `INT_MIN % -1 = 0` |
| 0x15 | `AND` | `rd, ra, rb` | bitwise and |
| 0x16 | `OR` | `rd, ra, rb` | bitwise or |
| 0x17 | `XOR` | `rd, ra, rb` | bitwise xor |
| 0x18 | `SHL` | `rd, ra, rb` | `rd = ra << (rb & 31)` |
| 0x19 | `SHR` | `rd, ra, rb` | `rd = ra >> (rb & 31)` (logical) |
| 0x1A | `ADDI` | `rd, ra, imm` | `rd = ra + simm` |
| 0x1B | `NOT` | `rd, ra` | `rd = ~ra` |
| 0x20 | `CMP` | `ra, rb` | set flags from `ra - rb` |
| 0x21 | `CMPI` | `ra, imm` | set flags from `ra - simm` |
| 0x30 | `JMP` | `addr` | `pc = addr` |
| 0x31 | `JZ` / `JE` | `addr` | jump if `Z` |
| 0x32 | `JNZ` / `JNE` | `addr` | jump if `!Z` |
| 0x33 | `JLT` | `addr` | jump if `N != V` (signed `<`) |
| 0x34 | `JGT` | `addr` | jump if `!Z && N == V` (signed `>`) |
| 0x35 | `JLE` | `addr` | jump if `Z || N != V` (signed `<=`) |
| 0x36 | `JGE` | `addr` | jump if `N == V` (signed `>=`) |
| 0x40 | `CALL` | `addr` | `sp -= 4; mem32[sp] = pc + 4; pc = addr` |
| 0x41 | `RET` | | `pc = mem32[sp]; sp += 4` |
| 0x42 | `PUSH` | `rd` | `sp -= 4; mem32[sp] = rd` |
| 0x43 | `POP` | `rd` | `rd = mem32[sp]; sp += 4` |
| 0x50 | `OUT` | `rd` | print `rd` as a signed decimal followed by `\n` |
| 0x51 | `OUTC` | `rd` | print the low byte of `rd` as a character (no newline) |

Flag computation for `CMP ra, rb` (with `r = ra - rb` in 32-bit arithmetic):

```
Z = (r == 0)    N = bit31(r)    C = (ra <u rb)    V = bit31((ra ^ rb) & (ra ^ r))
```

## Faults

A fault stops the machine and leaves `pc` pointing at the faulting instruction; registers and
memory are not modified by the faulting instruction.

| Status | Cause |
|--------|-------|
| `ERR_OPCODE` | opcode byte is not in the table |
| `ERR_DIV_ZERO` | `DIV`/`MOD` with a zero divisor |
| `ERR_MEM` | instruction fetch, load, store, `CALL`/`RET`/`PUSH`/`POP` touches an address outside 0–0xFFFF |

## Assembly language

```asm
; comment
label:      MNEMONIC  operand, operand      ; one instruction per line
            .org   ADDR                     ; set the location counter
            .equ   NAME, VALUE              ; define a constant
            .word  1, -1, label             ; 32-bit little-endian values
            .byte  1, 'A'                   ; bytes
            .string "text\n"                ; NUL-terminated string (escapes: \n \t \r \0 \\ \" \')
            .space N                        ; skip N bytes (zero-filled)
            .align N                        ; pad to a multiple of N
```

* Mnemonics and register names are case-insensitive; labels and constants are case-sensitive.
* Numbers: decimal, `0x` hex, `0b` binary, or a character literal such as `'A'`.
* Expressions are sums/differences of numbers and symbols (`array+4`, `N-1`).
* Memory operands are `[reg]`, `[reg+expr]` or `[reg-expr]`.
* Instructions must start on a 4-byte boundary (use `.align 4` after `.byte`/`.string` data).

### Conventions (not enforced by hardware)

* Arguments and return value in `r0`; `r1`–`r9` are caller-saved scratch.
* Put data after the code (or use `.org`), and load addresses with `LDI rX, label`.
