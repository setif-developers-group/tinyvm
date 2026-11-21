"""ctypes binding for the TinyVM C engine, plus small helpers for tests."""
import ctypes
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from asm import assemble  # noqa: E402

OK, HALTED, ERR_OPCODE, ERR_DIV_ZERO, ERR_MEM = range(5)
FLAG_Z, FLAG_N, FLAG_C, FLAG_V = 1, 2, 4, 8


def _find_library():
    for name in ("libvm.so", "libvm.dylib", "vm.dll", "libvm.dll"):
        p = ROOT / "build" / name
        if p.exists():
            return p
    raise FileNotFoundError("TinyVM shared library not found - run scripts/build.sh c first")


def _load():
    lib = ctypes.CDLL(str(_find_library()))
    vp, u32, u64, st = ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint64, ctypes.c_size_t
    sig = {
        "vm_create": (vp, []), "vm_destroy": (None, [vp]), "vm_reset": (None, [vp]),
        "vm_load": (ctypes.c_int, [vp, ctypes.c_char_p, st, u32]),
        "vm_step": (ctypes.c_int, [vp]), "vm_run": (ctypes.c_int, [vp, u64]),
        "vm_get_reg": (u32, [vp, ctypes.c_int]), "vm_set_reg": (None, [vp, ctypes.c_int, u32]),
        "vm_get_pc": (u32, [vp]), "vm_get_flags": (u32, [vp]),
        "vm_get_status": (ctypes.c_int, [vp]), "vm_get_cycles": (u64, [vp]),
        "vm_read_mem": (ctypes.c_int, [vp, u32, ctypes.c_char_p, st]),
        "vm_take_output": (st, [vp, ctypes.c_char_p, st]),
        "vm_disasm": (ctypes.c_int, [u32, ctypes.c_char_p, st]),
    }
    for name, (res, args) in sig.items():
        fn = getattr(lib, name)
        fn.restype, fn.argtypes = res, args
    return lib


_LIB = _load()


class VM:
    def __init__(self):
        self.h = _LIB.vm_create()

    def __del__(self):
        if getattr(self, "h", None):
            _LIB.vm_destroy(self.h)
            self.h = None

    def load(self, image, addr=0):
        return _LIB.vm_load(self.h, bytes(image), len(image), addr)

    def step(self):
        return _LIB.vm_step(self.h)

    def run(self, max_cycles=100_000):
        return _LIB.vm_run(self.h, max_cycles)

    def reset(self):
        _LIB.vm_reset(self.h)

    def reg(self, i):
        return _LIB.vm_get_reg(self.h, i)

    def sreg(self, i):
        v = self.reg(i)
        return v - (1 << 32) if v & 0x80000000 else v

    def set_reg(self, i, v):
        _LIB.vm_set_reg(self.h, i, v)

    @property
    def pc(self): return _LIB.vm_get_pc(self.h)
    @property
    def flags(self): return _LIB.vm_get_flags(self.h)
    @property
    def status(self): return _LIB.vm_get_status(self.h)
    @property
    def cycles(self): return _LIB.vm_get_cycles(self.h)

    def mem(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        if _LIB.vm_read_mem(self.h, addr, buf, n) != 0:
            raise IndexError("memory read out of range")
        return buf.raw

    def word(self, addr):
        return int.from_bytes(self.mem(addr, 4), "little")

    def output(self):
        buf = ctypes.create_string_buffer(4096)
        n = _LIB.vm_take_output(self.h, buf, 4096)
        return buf.raw[:n].decode("latin-1")


def disasm(word):
    buf = ctypes.create_string_buffer(64)
    _LIB.vm_disasm(word, buf, 64)
    return buf.value.decode()


def run_asm(source, max_cycles=100_000):
    """Assemble `source`, load it at 0 and run it. Returns the VM."""
    vm = VM()
    assert vm.load(assemble(source)) == 0
    vm.run(max_cycles)
    return vm
