/* vmrun - minimal command-line runner for TinyVM binaries. */
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_stdout(const char *d, size_t n, void *user) {
    (void)user;
    fwrite(d, 1, n, stdout);
}

static void dump_regs(const vm_t *vm, FILE *f) {
    for (int i = 0; i < VM_NUM_REGS; i++) {
        char name[8];
        if (i == VM_REG_SP) snprintf(name, sizeof name, "sp");
        else snprintf(name, sizeof name, "r%d", i);
        fprintf(f, "%-3s= 0x%08X%s", name, vm->regs[i], (i % 4 == 3) ? "\n" : "   ");
    }
    fprintf(f, "pc = 0x%04X   flags = %c%c%c%c   cycles = %llu   status = %s\n", vm->pc,
            (vm->flags & VM_FLAG_Z) ? 'Z' : '-', (vm->flags & VM_FLAG_N) ? 'N' : '-',
            (vm->flags & VM_FLAG_C) ? 'C' : '-', (vm->flags & VM_FLAG_V) ? 'V' : '-',
            (unsigned long long)vm->cycles, vm_status_str(vm->status));
}

static void usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [options] program.bin\n"
            "  -t          trace every instruction to stderr\n"
            "  -r          dump registers when execution stops\n"
            "  -m N        stop after N cycles (default: unlimited)\n"
            "  -h          show this help\n"
            "Exit codes: 0 halted, 1 fault, 2 usage/load error, 3 cycle limit reached\n", prog);
}

int main(int argc, char **argv) {
    int trace = 0, regs = 0;
    uint64_t max_cycles = 0;
    const char *path = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-t")) trace = 1;
        else if (!strcmp(argv[i], "-r")) regs = 1;
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) max_cycles = strtoull(argv[++i], NULL, 0);
        else if (!strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (argv[i][0] == '-') { usage(argv[0]); return 2; }
        else path = argv[i];
    }
    if (!path) { usage(argv[0]); return 2; }

    vm_t *vm = vm_create();
    if (!vm) { fprintf(stderr, "out of memory\n"); return 2; }
    vm->out_fn = write_stdout;

    if (vm_load_file(vm, path, 0) < 0) {
        fprintf(stderr, "error: cannot load '%s' (missing, or larger than %u bytes)\n", path, VM_MEM_SIZE);
        vm_destroy(vm);
        return 2;
    }

    if (trace) {
        char text[64];
        uint64_t n = 0;
        while (vm->status == VM_OK && (max_cycles == 0 || n < max_cycles)) {
            vm_disasm_at(vm, vm->pc, text, sizeof text);
            fprintf(stderr, "[%6llu] %04X: %s\n", (unsigned long long)vm->cycles, vm->pc, text);
            vm_step(vm);
            n++;
        }
    } else {
        vm_run(vm, max_cycles);
    }

    fflush(stdout);
    if (regs) dump_regs(vm, stderr);

    int code = 0;
    if (vm->status == VM_OK) { fprintf(stderr, "stopped: cycle limit reached\n"); code = 3; }
    else if (vm->status != VM_HALTED) { fprintf(stderr, "%s at pc=0x%04X\n", vm_status_str(vm->status), vm->pc); code = 1; }

    vm_destroy(vm);
    return code;
}
