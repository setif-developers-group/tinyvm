# TinyVM

**A tiny 32-bit virtual machine implemented across seven languages.**

TinyVM is a small CPU emulator with its own instruction set, assembler, debugger, desktop dashboard and
browser version. Each language owns one clearly defined part of the system, which makes the repo a
compact tour of how a real toolchain fits together.

| Language | Role | Location |
|----------|------|----------|
| **C** | High-performance core engine: fetch-decode-execute loop, memory model, disassembler | [`src/c/`](src/c) |
| **C++** | RAII wrapper + interactive monitor/debugger (registers, memory, breakpoints) | [`src/cpp/`](src/cpp) |
| **C# (.NET)** | WinForms dashboard that drives the C engine through P/Invoke | [`gui/VmDashboard/`](gui/VmDashboard) |
| **Python** | Assembler and the automated test-suite (via `ctypes`) | [`tools/`](tools), [`tests/`](tests) |
| **JavaScript** | Self-contained browser version: VM, assembler and UI | [`web/`](web) |
| **Bash** | Build, run and test automation | [`scripts/`](scripts) |
| **Assembly** | Example programs in TinyVM's own ISA | [`examples/`](examples) |

## Architecture

```
                 examples/*.asm
                       │  tools/asm.py  (Python assembler)
                       ▼
                  program.bin ───────────────┬──────────────────────┐
                       │                     │                      │
                       ▼                     ▼                      ▼
        ┌──────────────────────────┐  web/vm.js            (your port here)
        │ C engine  src/c/vm.c     │  JavaScript VM
        │ libvm.so / vm.dll        │  + in-browser assembler
        └───┬───────────┬──────────┘
            │           │ P/Invoke
   static   │           ▼
   link     ▼     C# dashboard (gui/)        ctypes ◄── Python tests (tests/)
   vmrun (CLI)    vmtool (C++ monitor)
```

## Repository layout

```
tinyvm/
├── README.md
├── LICENSE
├── docs/
│   └── ISA.md                # normative instruction-set specification
├── src/
│   ├── c/                    # vm.h, vm.c (engine), main.c (vmrun CLI)
│   └── cpp/                  # vmtool.cpp (monitor / debugger)
├── gui/VmDashboard/          # .NET 8 WinForms dashboard
├── web/                      # index.html, vm.js, app.js, style.css, test.js
├── tools/asm.py              # assembler
├── examples/                 # *.asm programs + *.out expected output
├── tests/                    # Python unittest suite
├── scripts/                  # build.sh, run.sh, test.sh, gen_web_examples.sh
└── .github/workflows/ci.yml
```

## Quick start

Requirements: `gcc` (or clang), `g++`, `python3`, and optionally `node` (JS tests) and the
.NET 8 SDK (dashboard, Windows).

```bash
git clone <your-repo-url> tinyvm && cd tinyvm

./scripts/build.sh            # builds vmrun, vmtool, the shared library, web bundle
./scripts/run.sh examples/fibonacci.asm
./scripts/test.sh             # Python + JavaScript + CLI checks
```

### Run a program (C engine)

```bash
./scripts/run.sh examples/factorial.asm          # assemble + run
./scripts/run.sh examples/factorial.asm -t -r    # trace each instruction, dump registers
```

Manual route:

```bash
python3 tools/asm.py examples/hello.asm -o build/hello.bin -l   # -l prints a listing
./build/vmrun build/hello.bin
```

`vmrun` options: `-t` trace, `-r` dump registers, `-m N` cycle limit. Exit codes: `0` halted,
`1` fault, `2` usage/load error, `3` cycle limit reached.

### Debug interactively (C++ monitor)

```bash
python3 tools/asm.py examples/sort.asm -o build/sort.bin
./build/vmtool build/sort.bin
(vm) break 0x1C
(vm) run
(vm) step 3          # shows the disassembly and every register that changed
(vm) regs
(vm) mem 0x0 64
(vm) help
```

### Browser version (JavaScript)

Open `web/index.html` in a browser — no server or build step needed. Pick an example, edit the
assembly, then **Assemble & Load**, **Step** or **Run**. Registers that change are highlighted.
(After editing `examples/*.asm`, run `./scripts/build.sh web` to refresh the bundled samples.)

### Desktop dashboard (C# / .NET 8, Windows)

The dashboard calls the C engine as a native library, so build `vm.dll` first (MinGW-w64 /
MSYS2 shell):

```bash
./scripts/build.sh c          # produces build/vm.dll on MSYS/MinGW
./scripts/build.sh gui        # or: dotnet run --project gui/VmDashboard -c Release
```

Use **Open .bin…** to load an assembled program, then **Step** or **Run**. The dashboard shows the
registers (changed ones highlighted), flags, a 256-byte memory window, the disassembled execution
trace and program output. Keep the architecture of `vm.dll` (x64) in line with your .NET runtime.

## The instruction set at a glance

32-bit fixed-width instructions, 16 registers (`r15` = `sp`), 64 KiB little-endian memory.

| Group | Instructions |
|-------|--------------|
| Control | `NOP` `HALT` |
| Data movement | `MOV` `LDI` `LD` `ST` `LDB` `STB` `PUSH` `POP` |
| Arithmetic / logic | `ADD` `SUB` `MUL` `DIV` `MOD` `AND` `OR` `XOR` `NOT` `SHL` `SHR` `ADDI` |
| Comparison | `CMP` `CMPI` (set Z/N/C/V) |
| Branching | `JMP` `JZ` `JNZ` `JLT` `JGT` `JLE` `JGE` `CALL` `RET` |
| I/O | `OUT` (signed decimal + newline) `OUTC` (character) |

Example (`examples/fibonacci.asm`):

```asm
        LDI   r1, 0              ; a
        LDI   r2, 1              ; b
        LDI   r3, 10             ; count
loop:   OUT   r1
        ADD   r4, r1, r2
        MOV   r1, r2
        MOV   r2, r4
        ADDI  r3, r3, -1
        CMPI  r3, 0
        JNZ   loop
        HALT
```

The full encoding, semantics, fault rules and assembler syntax live in [`docs/ISA.md`](docs/ISA.md).

## Testing

```bash
./scripts/test.sh
```

* `tests/test_assembler.py` — encodings, labels, directives, error reporting.
* `tests/test_vm.py` — each instruction, flags, signed/unsigned edge cases, faults, stack behaviour,
  disassembler round-trips. These run against the real C engine through `ctypes`.
* `tests/test_examples.py` and `web/test.js` — run every `examples/*.asm` and compare the output with
  the matching `examples/*.out`. This is the **conformance suite** shared by every implementation.

## Adding an implementation in another language

TinyVM is designed so that a new port is a well-bounded task.

1. **Read the spec.** [`docs/ISA.md`](docs/ISA.md) is normative; `src/c/vm.c` is the reference
   implementation. Pay attention to edge cases: sign extension, `INT_MIN / -1`, shift masking,
   little-endian memory, fault behaviour.
2. **Create a folder** under `src/<language>/` (or `ports/<language>/` for a full alternative VM)
   with its own short README and build instructions.
3. **Pick a level of integration:**
   * *Binding* — call the C shared library (`build/libvm.so`, `vm.dll`, `libvm.dylib`). The flat C API
     (`vm_create`, `vm_load`, `vm_step`, `vm_get_reg`, `vm_read_mem`, `vm_take_output`, …) is declared in
     `src/c/vm.h` and is easy to reach from Rust, Go, Java, Ruby, Swift, Lua, etc.
   * *Native port* — reimplement the machine (see `web/vm.js` for a compact, dependency-free example).
4. **Pass the conformance suite.** Your implementation must run every `examples/*.asm` (assembled with
   `tools/asm.py`) and produce exactly the bytes in the matching `.out` file. See `web/test.js` for a minimal
   harness.
5. **Wire it in.** Add a `build_<language>` function to `scripts/build.sh`, a step in `scripts/test.sh`,
   and a row in the language table above.

Ideas: a Rust or Go port, a WebAssembly build of the C core, a Java/Kotlin GUI, a Lua scripting API,
an assembler written in C.

## Contributing

* Open an issue before large changes; small fixes are welcome as direct pull requests.
* New instructions must update **all** of: `docs/ISA.md`, `src/c/vm.h` + `vm.c`, `tools/asm.py`,
  `web/vm.js`, and add tests.
* Add example programs to `examples/` together with an `.out` file (`scripts/test.sh` will pick them up).
* Run `./scripts/test.sh` before submitting. CI runs the same script.
* Keep the C core warning-free under `-Wall -Wextra -pedantic`.

## Roadmap

- [ ] Interrupts and a memory-mapped timer
- [ ] Input instruction (`IN`) and a keyboard device
- [ ] Source-level labels in the debugger (assembler symbol-table output)
- [ ] Cross-platform GUI (Avalonia) in place of WinForms
- [ ] WebAssembly build of the C core

## License

MIT — see [`LICENSE`](LICENSE).
