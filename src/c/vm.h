/*
 * TinyVM - core engine (C11)
 *
 * A small 32-bit register machine: 16 registers, 64 KiB of flat byte-addressed
 * memory, fixed-width 32-bit instructions. See docs/ISA.md for the full spec.
 */
#ifndef TINYVM_VM_H
#define TINYVM_VM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#  ifdef VM_BUILD_DLL
#    define VM_API __declspec(dllexport)
#  else
#    define VM_API
#  endif
#elif defined(__GNUC__)
#  define VM_API __attribute__((visibility("default")))
#else
#  define VM_API
#endif

#define VM_MEM_SIZE  65536u
#define VM_NUM_REGS  16
#define VM_REG_SP    15
#define VM_OUT_BUF   4096

/* Opcodes ---------------------------------------------------------------- */
typedef enum {
    OP_NOP  = 0x00, OP_HALT = 0x01,
    OP_MOV  = 0x02, OP_LDI  = 0x03,
    OP_LD   = 0x05, OP_ST   = 0x06, OP_LDB = 0x07, OP_STB = 0x08,
    OP_ADD  = 0x10, OP_SUB  = 0x11, OP_MUL = 0x12, OP_DIV = 0x13, OP_MOD = 0x14,
    OP_AND  = 0x15, OP_OR   = 0x16, OP_XOR = 0x17, OP_SHL = 0x18, OP_SHR = 0x19,
    OP_ADDI = 0x1A, OP_NOT  = 0x1B,
    OP_CMP  = 0x20, OP_CMPI = 0x21,
    OP_JMP  = 0x30, OP_JZ   = 0x31, OP_JNZ = 0x32,
    OP_JLT  = 0x33, OP_JGT  = 0x34, OP_JLE = 0x35, OP_JGE = 0x36,
    OP_CALL = 0x40, OP_RET  = 0x41, OP_PUSH = 0x42, OP_POP = 0x43,
    OP_OUT  = 0x50, OP_OUTC = 0x51
} vm_opcode;

/* Machine status --------------------------------------------------------- */
typedef enum {
    VM_OK = 0,            /* running / ready                    */
    VM_HALTED = 1,        /* HALT executed                      */
    VM_ERR_OPCODE = 2,    /* unknown opcode                     */
    VM_ERR_DIV_ZERO = 3,  /* DIV / MOD by zero                  */
    VM_ERR_MEM = 4        /* out-of-range memory/stack/fetch    */
} vm_status;

/* Flag bits (set by CMP / CMPI only) -------------------------------------- */
#define VM_FLAG_Z 0x1u  /* zero            */
#define VM_FLAG_N 0x2u  /* negative        */
#define VM_FLAG_C 0x4u  /* unsigned borrow */
#define VM_FLAG_V 0x8u  /* signed overflow */

/* Called for every OUT/OUTC when set; otherwise output is buffered. */
typedef void (*vm_out_fn)(const char *data, size_t len, void *user);

typedef struct vm {
    uint32_t  regs[VM_NUM_REGS];
    uint32_t  pc;
    uint32_t  flags;
    vm_status status;
    uint64_t  cycles;
    uint8_t   mem[VM_MEM_SIZE];
    vm_out_fn out_fn;
    void     *out_user;
    char      out_buf[VM_OUT_BUF];
    size_t    out_len;
} vm_t;

/* Lifecycle -------------------------------------------------------------- */
VM_API vm_t      *vm_create(void);
VM_API void       vm_destroy(vm_t *vm);
VM_API void       vm_reset(vm_t *vm);   /* clears CPU *and* memory; SP = 0x10000 */

/* Loading ---------------------------------------------------------------- */
VM_API int        vm_load(vm_t *vm, const uint8_t *data, size_t size, uint32_t addr);
VM_API long       vm_load_file(vm_t *vm, const char *path, uint32_t addr);

/* Execution -------------------------------------------------------------- */
VM_API vm_status  vm_step(vm_t *vm);                         /* one instruction */
VM_API vm_status  vm_run(vm_t *vm, uint64_t max_cycles);     /* 0 = unlimited   */

/* Inspection (flat accessors, convenient for FFI / P/Invoke / ctypes) ------ */
VM_API uint32_t   vm_get_reg(const vm_t *vm, int index);
VM_API void       vm_set_reg(vm_t *vm, int index, uint32_t value);
VM_API uint32_t   vm_get_pc(const vm_t *vm);
VM_API uint32_t   vm_get_flags(const vm_t *vm);
VM_API int        vm_get_status(const vm_t *vm);
VM_API uint64_t   vm_get_cycles(const vm_t *vm);
VM_API int        vm_read_mem(const vm_t *vm, uint32_t addr, uint8_t *out, size_t len);
VM_API size_t     vm_take_output(vm_t *vm, char *out, size_t cap);

/* Utilities -------------------------------------------------------------- */
VM_API int        vm_disasm(uint32_t instruction, char *buf, size_t cap);
VM_API int        vm_disasm_at(const vm_t *vm, uint32_t addr, char *buf, size_t cap);
VM_API const char *vm_status_str(int status);

#ifdef __cplusplus
}
#endif
#endif /* TINYVM_VM_H */
