/* TinyVM core engine: fetch-decode-execute loop, memory model, disassembler. */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Opcode table (used by the disassembler)
 * ------------------------------------------------------------------------- */
typedef enum { F_NONE, F_R, F_RR, F_RI, F_MEM, F_RRR, F_RRI, F_CMP, F_CMPI, F_ADDR } fmt_t;
typedef struct { uint8_t op; const char *name; fmt_t fmt; } op_info_t;

static const op_info_t OPS[] = {
    {OP_NOP, "NOP", F_NONE}, {OP_HALT, "HALT", F_NONE},
    {OP_MOV, "MOV", F_RR},   {OP_LDI, "LDI", F_RI},
    {OP_LD, "LD", F_MEM},    {OP_ST, "ST", F_MEM},
    {OP_LDB, "LDB", F_MEM},  {OP_STB, "STB", F_MEM},
    {OP_ADD, "ADD", F_RRR},  {OP_SUB, "SUB", F_RRR}, {OP_MUL, "MUL", F_RRR},
    {OP_DIV, "DIV", F_RRR},  {OP_MOD, "MOD", F_RRR}, {OP_AND, "AND", F_RRR},
    {OP_OR, "OR", F_RRR},    {OP_XOR, "XOR", F_RRR}, {OP_SHL, "SHL", F_RRR},
    {OP_SHR, "SHR", F_RRR},  {OP_ADDI, "ADDI", F_RRI}, {OP_NOT, "NOT", F_RR},
    {OP_CMP, "CMP", F_CMP},  {OP_CMPI, "CMPI", F_CMPI},
    {OP_JMP, "JMP", F_ADDR}, {OP_JZ, "JZ", F_ADDR},  {OP_JNZ, "JNZ", F_ADDR},
    {OP_JLT, "JLT", F_ADDR}, {OP_JGT, "JGT", F_ADDR}, {OP_JLE, "JLE", F_ADDR},
    {OP_JGE, "JGE", F_ADDR},
    {OP_CALL, "CALL", F_ADDR}, {OP_RET, "RET", F_NONE},
    {OP_PUSH, "PUSH", F_R},  {OP_POP, "POP", F_R},
    {OP_OUT, "OUT", F_R},    {OP_OUTC, "OUTC", F_R},
};

static const op_info_t *lookup_op(uint8_t op) {
    for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; i++)
        if (OPS[i].op == op) return &OPS[i];
    return NULL;
}

static const char *const REG_NAMES[VM_NUM_REGS] = {
    "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "sp"
};

/* ---------------------------------------------------------------------------
 * Memory helpers (little-endian, bounds-checked, unaligned access allowed)
 * ------------------------------------------------------------------------- */
static int read32(const vm_t *vm, uint32_t addr, uint32_t *out) {
    if ((uint64_t)addr + 4 > VM_MEM_SIZE) return -1;
    const uint8_t *p = &vm->mem[addr];
    *out = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return 0;
}

static int write32(vm_t *vm, uint32_t addr, uint32_t v) {
    if ((uint64_t)addr + 4 > VM_MEM_SIZE) return -1;
    uint8_t *p = &vm->mem[addr];
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
    return 0;
}

static void emit(vm_t *vm, const char *s, size_t n) {
    if (vm->out_fn) { vm->out_fn(s, n, vm->out_user); return; }
    size_t room = VM_OUT_BUF - vm->out_len;
    if (n > room) n = room;               /* drop overflow rather than crash */
    memcpy(vm->out_buf + vm->out_len, s, n);
    vm->out_len += n;
}

static vm_status fault(vm_t *vm, vm_status s) { vm->status = s; return s; }

static void set_sub_flags(vm_t *vm, uint32_t a, uint32_t b) {
    uint32_t r = a - b, f = 0;
    if (r == 0)                          f |= VM_FLAG_Z;
    if (r >> 31)                         f |= VM_FLAG_N;
    if (a < b)                           f |= VM_FLAG_C;
    if (((a ^ b) & (a ^ r)) >> 31)       f |= VM_FLAG_V;
    vm->flags = f;
}

/* ---------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */
void vm_reset(vm_t *vm) {
    vm_out_fn fn = vm->out_fn;
    void *user = vm->out_user;
    memset(vm, 0, sizeof *vm);
    vm->out_fn = fn;
    vm->out_user = user;
    vm->regs[VM_REG_SP] = VM_MEM_SIZE;    /* empty descending stack */
}

vm_t *vm_create(void) {
    vm_t *vm = (vm_t *)calloc(1, sizeof *vm);
    if (vm) vm_reset(vm);
    return vm;
}

void vm_destroy(vm_t *vm) { free(vm); }

int vm_load(vm_t *vm, const uint8_t *data, size_t size, uint32_t addr) {
    if (!vm || (!data && size) || (uint64_t)addr + size > VM_MEM_SIZE) return -1;
    if (size) memcpy(&vm->mem[addr], data, size);
    return 0;
}

long vm_load_file(vm_t *vm, const char *path, uint32_t addr) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    rewind(f);
    if (size < 0 || (uint64_t)addr + (uint64_t)size > VM_MEM_SIZE) { fclose(f); return -1; }
    size_t got = fread(&vm->mem[addr], 1, (size_t)size, f);
    fclose(f);
    return got == (size_t)size ? size : -1;
}

/* ---------------------------------------------------------------------------
 * Fetch - decode - execute
 *
 *   31      24 23   20 19   16 15   12 11            0
 *  +----------+-------+-------+-------+---------------+
 *  |  opcode  |  rd   |  ra   |  rb   |  (imm16 spans bits 15..0)
 *  +----------+-------+-------+-------+---------------+
 * ------------------------------------------------------------------------- */
vm_status vm_step(vm_t *vm) {
    if (vm->status != VM_OK) return vm->status;

    uint32_t ins;
    if (read32(vm, vm->pc, &ins) != 0) return fault(vm, VM_ERR_MEM);

    const uint8_t  op   = (uint8_t)(ins >> 24);
    const unsigned rd   = (ins >> 20) & 0xF;
    const unsigned ra   = (ins >> 16) & 0xF;
    const unsigned rb   = (ins >> 12) & 0xF;
    const uint16_t imm  = (uint16_t)(ins & 0xFFFF);
    const int32_t  simm = (int16_t)imm;
    uint32_t *R = vm->regs;
    uint32_t next = vm->pc + 4, v;

    const int z = (vm->flags & VM_FLAG_Z) != 0;
    const int n = (vm->flags & VM_FLAG_N) != 0;
    const int ov = (vm->flags & VM_FLAG_V) != 0;

    switch (op) {
    case OP_NOP:  break;
    case OP_HALT: vm->status = VM_HALTED; break;
    case OP_MOV:  R[rd] = R[ra]; break;
    case OP_LDI:  R[rd] = (uint32_t)simm; break;

    case OP_LD:
        if (read32(vm, R[ra] + (uint32_t)simm, &v)) return fault(vm, VM_ERR_MEM);
        R[rd] = v; break;
    case OP_ST:
        if (write32(vm, R[ra] + (uint32_t)simm, R[rd])) return fault(vm, VM_ERR_MEM);
        break;
    case OP_LDB: {
        uint32_t a = R[ra] + (uint32_t)simm;
        if (a >= VM_MEM_SIZE) return fault(vm, VM_ERR_MEM);
        R[rd] = vm->mem[a]; break;
    }
    case OP_STB: {
        uint32_t a = R[ra] + (uint32_t)simm;
        if (a >= VM_MEM_SIZE) return fault(vm, VM_ERR_MEM);
        vm->mem[a] = (uint8_t)R[rd]; break;
    }

    case OP_ADD:  R[rd] = R[ra] + R[rb]; break;
    case OP_SUB:  R[rd] = R[ra] - R[rb]; break;
    case OP_MUL:  R[rd] = R[ra] * R[rb]; break;
    case OP_DIV:
    case OP_MOD: {
        int32_t a = (int32_t)R[ra], b = (int32_t)R[rb];
        if (b == 0) return fault(vm, VM_ERR_DIV_ZERO);
        if (a == INT32_MIN && b == -1) R[rd] = (op == OP_DIV) ? (uint32_t)INT32_MIN : 0u;
        else R[rd] = (uint32_t)(op == OP_DIV ? a / b : a % b);
        break;
    }
    case OP_AND:  R[rd] = R[ra] & R[rb]; break;
    case OP_OR:   R[rd] = R[ra] | R[rb]; break;
    case OP_XOR:  R[rd] = R[ra] ^ R[rb]; break;
    case OP_SHL:  R[rd] = R[ra] << (R[rb] & 31); break;
    case OP_SHR:  R[rd] = R[ra] >> (R[rb] & 31); break;   /* logical */
    case OP_ADDI: R[rd] = R[ra] + (uint32_t)simm; break;
    case OP_NOT:  R[rd] = ~R[ra]; break;

    case OP_CMP:  set_sub_flags(vm, R[ra], R[rb]); break;
    case OP_CMPI: set_sub_flags(vm, R[ra], (uint32_t)simm); break;

    case OP_JMP:  next = imm; break;
    case OP_JZ:   if (z)               next = imm; break;
    case OP_JNZ:  if (!z)              next = imm; break;
    case OP_JLT:  if (n != ov)         next = imm; break;
    case OP_JGE:  if (n == ov)         next = imm; break;
    case OP_JGT:  if (!z && n == ov)   next = imm; break;
    case OP_JLE:  if (z || n != ov)    next = imm; break;

    case OP_CALL:
        v = R[VM_REG_SP] - 4;
        if (write32(vm, v, next)) return fault(vm, VM_ERR_MEM);
        R[VM_REG_SP] = v; next = imm; break;
    case OP_RET:
        if (read32(vm, R[VM_REG_SP], &v)) return fault(vm, VM_ERR_MEM);
        R[VM_REG_SP] += 4; next = v; break;
    case OP_PUSH:
        v = R[VM_REG_SP] - 4;
        if (write32(vm, v, R[rd])) return fault(vm, VM_ERR_MEM);
        R[VM_REG_SP] = v; break;
    case OP_POP:
        if (read32(vm, R[VM_REG_SP], &v)) return fault(vm, VM_ERR_MEM);
        R[VM_REG_SP] += 4; R[rd] = v; break;

    case OP_OUT: {
        char tmp[16];
        int len = snprintf(tmp, sizeof tmp, "%d\n", (int)(int32_t)R[rd]);
        emit(vm, tmp, (size_t)len); break;
    }
    case OP_OUTC: {
        char c = (char)(R[rd] & 0xFF);
        emit(vm, &c, 1); break;
    }
    default:
        return fault(vm, VM_ERR_OPCODE);
    }

    vm->pc = next;
    vm->cycles++;
    return vm->status;
}

vm_status vm_run(vm_t *vm, uint64_t max_cycles) {
    uint64_t n = 0;
    while (vm->status == VM_OK && (max_cycles == 0 || n < max_cycles)) {
        vm_step(vm);
        n++;
    }
    return vm->status;
}

/* ---------------------------------------------------------------------------
 * Accessors
 * ------------------------------------------------------------------------- */
uint32_t vm_get_reg(const vm_t *vm, int i) { return (i >= 0 && i < VM_NUM_REGS) ? vm->regs[i] : 0; }
void     vm_set_reg(vm_t *vm, int i, uint32_t v) { if (i >= 0 && i < VM_NUM_REGS) vm->regs[i] = v; }
uint32_t vm_get_pc(const vm_t *vm)     { return vm->pc; }
uint32_t vm_get_flags(const vm_t *vm)  { return vm->flags; }
int      vm_get_status(const vm_t *vm) { return (int)vm->status; }
uint64_t vm_get_cycles(const vm_t *vm) { return vm->cycles; }

int vm_read_mem(const vm_t *vm, uint32_t addr, uint8_t *out, size_t len) {
    if ((uint64_t)addr + len > VM_MEM_SIZE) return -1;
    memcpy(out, &vm->mem[addr], len);
    return 0;
}

size_t vm_take_output(vm_t *vm, char *out, size_t cap) {
    size_t n = vm->out_len < cap ? vm->out_len : cap;
    memcpy(out, vm->out_buf, n);
    memmove(vm->out_buf, vm->out_buf + n, vm->out_len - n);
    vm->out_len -= n;
    return n;
}

const char *vm_status_str(int s) {
    switch (s) {
    case VM_OK:           return "running";
    case VM_HALTED:       return "halted";
    case VM_ERR_OPCODE:   return "fault: illegal opcode";
    case VM_ERR_DIV_ZERO: return "fault: division by zero";
    case VM_ERR_MEM:      return "fault: memory access out of range";
    default:              return "unknown";
    }
}

/* ---------------------------------------------------------------------------
 * Disassembler
 * ------------------------------------------------------------------------- */
int vm_disasm(uint32_t ins, char *buf, size_t cap) {
    const op_info_t *oi = lookup_op((uint8_t)(ins >> 24));
    if (!oi) return snprintf(buf, cap, ".word 0x%08X", ins);

    const char *rd = REG_NAMES[(ins >> 20) & 0xF];
    const char *ra = REG_NAMES[(ins >> 16) & 0xF];
    const char *rb = REG_NAMES[(ins >> 12) & 0xF];
    const unsigned imm = ins & 0xFFFF;
    const int simm = (int16_t)imm;

    switch (oi->fmt) {
    case F_NONE: return snprintf(buf, cap, "%s", oi->name);
    case F_R:    return snprintf(buf, cap, "%-5s %s", oi->name, rd);
    case F_RR:   return snprintf(buf, cap, "%-5s %s, %s", oi->name, rd, ra);
    case F_RI:   return snprintf(buf, cap, "%-5s %s, %d", oi->name, rd, simm);
    case F_RRR:  return snprintf(buf, cap, "%-5s %s, %s, %s", oi->name, rd, ra, rb);
    case F_RRI:  return snprintf(buf, cap, "%-5s %s, %s, %d", oi->name, rd, ra, simm);
    case F_CMP:  return snprintf(buf, cap, "%-5s %s, %s", oi->name, ra, rb);
    case F_CMPI: return snprintf(buf, cap, "%-5s %s, %d", oi->name, ra, simm);
    case F_ADDR: return snprintf(buf, cap, "%-5s 0x%04X", oi->name, imm);
    case F_MEM:
        if (simm == 0) return snprintf(buf, cap, "%-5s %s, [%s]", oi->name, rd, ra);
        return snprintf(buf, cap, "%-5s %s, [%s%c%d]", oi->name, rd, ra, simm < 0 ? '-' : '+', simm < 0 ? -simm : simm);
    }
    return 0;
}

int vm_disasm_at(const vm_t *vm, uint32_t addr, char *buf, size_t cap) {
    uint32_t ins;
    if (read32(vm, addr, &ins) != 0) return snprintf(buf, cap, "<out of range>");
    return vm_disasm(ins, buf, cap);
}
