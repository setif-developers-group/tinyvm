// vmtool - interactive monitor / debugger for TinyVM (C++17).
//
// An object-oriented RAII wrapper (Machine) around the C engine, plus a small
// REPL for stepping, breakpoints, register and memory inspection.
#include "vm.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

class Machine {
public:
    Machine() : vm_(vm_create()) {
        if (!vm_) throw std::runtime_error("out of memory");
        vm_->out_fn = &Machine::onOutput;
    }
    ~Machine() { vm_destroy(vm_); }
    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;

    bool load(const std::string& path) {
        vm_reset(vm_);
        long n = vm_load_file(vm_, path.c_str(), 0);
        if (n < 0) return false;
        path_ = path;
        size_ = static_cast<std::size_t>(n);
        return true;
    }
    void reset() {
        if (path_.empty()) vm_reset(vm_);
        else load(path_);
    }

    const std::string& path() const { return path_; }
    std::size_t imageSize() const { return size_; }
    uint32_t pc() const { return vm_get_pc(vm_); }
    uint32_t reg(int i) const { return vm_get_reg(vm_, i); }
    uint32_t flags() const { return vm_get_flags(vm_); }
    uint64_t cycles() const { return vm_get_cycles(vm_); }
    vm_status status() const { return static_cast<vm_status>(vm_get_status(vm_)); }
    bool running() const { return status() == VM_OK; }

    std::array<uint32_t, VM_NUM_REGS> registers() const {
        std::array<uint32_t, VM_NUM_REGS> r{};
        for (int i = 0; i < VM_NUM_REGS; i++) r[i] = reg(i);
        return r;
    }

    std::string disasm(uint32_t addr) const {
        char buf[64];
        vm_disasm_at(vm_, addr, buf, sizeof buf);
        return buf;
    }

    std::vector<uint8_t> readMem(uint32_t addr, std::size_t len) const {
        std::vector<uint8_t> out(len);
        if (vm_read_mem(vm_, addr, out.data(), len) != 0) out.clear();
        return out;
    }

    vm_status step() { return vm_step(vm_); }

    // Run until halt/fault, cycle budget exhausted, or a breakpoint is reached.
    // Returns true if stopped on a breakpoint.
    bool run(uint64_t maxCycles, const std::set<uint32_t>& breaks) {
        uint64_t n = 0;
        bool first = true;
        while (running() && (maxCycles == 0 || n < maxCycles)) {
            if (!first && breaks.count(pc())) return true;
            first = false;
            vm_step(vm_);
            n++;
        }
        return false;
    }

private:
    static void onOutput(const char* data, std::size_t len, void*) { std::cout.write(data, static_cast<std::streamsize>(len)); }
    vm_t* vm_;
    std::string path_;
    std::size_t size_ = 0;
};

namespace {

std::string hex(uint32_t v, int width) {
    std::ostringstream o;
    o << "0x" << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << v;
    return o.str();
}

std::string regName(int i) { return i == VM_REG_SP ? "sp" : "r" + std::to_string(i); }

std::string flagString(uint32_t f) {
    std::string s = "----";
    if (f & VM_FLAG_Z) s[0] = 'Z';
    if (f & VM_FLAG_N) s[1] = 'N';
    if (f & VM_FLAG_C) s[2] = 'C';
    if (f & VM_FLAG_V) s[3] = 'V';
    return s;
}

bool parseNum(const std::string& s, uint32_t& out) {
    char* end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 0);
    if (s.empty() || *end != '\0') return false;
    out = static_cast<uint32_t>(v);
    return true;
}

void printRegs(const Machine& m) {
    for (int i = 0; i < VM_NUM_REGS; i++) {
        std::cout << std::left << std::setw(4) << regName(i) << hex(m.reg(i), 8) << "  "
                  << std::right << std::setw(11) << static_cast<int32_t>(m.reg(i))
                  << ((i % 2) ? "\n" : "    ");
    }
    std::cout << "pc   " << hex(m.pc(), 4) << "    flags " << flagString(m.flags())
              << "    cycles " << m.cycles() << "    " << vm_status_str(m.status()) << "\n";
}

void printMem(const Machine& m, uint32_t addr, uint32_t len) {
    auto bytes = m.readMem(addr, len);
    if (bytes.empty() && len) { std::cout << "address range out of bounds\n"; return; }
    for (std::size_t i = 0; i < bytes.size(); i += 16) {
        std::cout << hex(addr + static_cast<uint32_t>(i), 4) << "  ";
        std::string ascii;
        for (std::size_t j = 0; j < 16; j++) {
            if (i + j < bytes.size()) {
                std::cout << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
                          << static_cast<int>(bytes[i + j]) << ' ' << std::dec;
                ascii += std::isprint(bytes[i + j]) ? static_cast<char>(bytes[i + j]) : '.';
            } else {
                std::cout << "   ";
            }
        }
        std::cout << " |" << ascii << "|\n";
    }
}

// Execute one instruction and show what it did.
void stepVerbose(Machine& m) {
    auto before = m.registers();
    uint32_t pc = m.pc();
    std::string text = m.disasm(pc);
    vm_status st = m.step();
    std::cout << hex(pc, 4) << ": " << std::left << std::setw(24) << text << std::right;
    for (int i = 0; i < VM_NUM_REGS; i++) {
        uint32_t now = m.reg(i);
        if (now != before[i]) std::cout << ' ' << regName(i) << ' ' << hex(before[i], 8) << "->" << hex(now, 8);
    }
    std::cout << '\n';
    if (st != VM_OK) std::cout << "** " << vm_status_str(st) << " **\n";
}

void help() {
    std::cout <<
        "Commands:\n"
        "  load <file.bin>      load a program image (resets the machine)\n"
        "  step [n]  (s)        execute n instructions (default 1), showing register changes\n"
        "  run [max]  (r)       run until halt / fault / breakpoint (optional cycle cap)\n"
        "  regs  (i)            show registers, flags, cycle count\n"
        "  mem <addr> [len]     hex dump memory (default 64 bytes)\n"
        "  dis [addr] [count]   disassemble (default: at pc, 8 instructions)\n"
        "  break <addr>         set breakpoint     unbreak <addr>   clear it\n"
        "  breaks               list breakpoints\n"
        "  reset                reload program and reset the CPU\n"
        "  help  (h, ?)         this text          quit  (q)\n";
}

}  // namespace

int main(int argc, char** argv) {
    Machine m;
    std::set<uint32_t> breaks;

    if (argc > 1) {
        if (!m.load(argv[1])) { std::cerr << "cannot load '" << argv[1] << "'\n"; return 2; }
        std::cout << "loaded " << m.imageSize() << " bytes from " << argv[1] << "\n";
    }
    std::cout << "TinyVM monitor - type 'help' for commands\n";

    std::string line;
    while (std::cout << "(vm) " << std::flush, std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string cmd;
        if (!(in >> cmd)) continue;
        std::vector<std::string> args;
        for (std::string a; in >> a;) args.push_back(a);
        uint32_t a0 = 0, a1 = 0;

        if (cmd == "quit" || cmd == "q" || cmd == "exit") break;
        else if (cmd == "help" || cmd == "h" || cmd == "?") help();
        else if (cmd == "load") {
            if (args.empty()) std::cout << "usage: load <file.bin>\n";
            else if (!m.load(args[0])) std::cout << "cannot load '" << args[0] << "'\n";
            else std::cout << "loaded " << m.imageSize() << " bytes\n";
        }
        else if (cmd == "reset") { m.reset(); std::cout << "machine reset\n"; }
        else if (cmd == "step" || cmd == "s") {
            uint32_t n = 1;
            if (!args.empty() && !parseNum(args[0], n)) { std::cout << "bad count\n"; continue; }
            for (uint32_t i = 0; i < n && m.running(); i++) stepVerbose(m);
            if (!m.running()) std::cout << "machine is " << vm_status_str(m.status()) << "\n";
        }
        else if (cmd == "run" || cmd == "r") {
            uint32_t cap = 0;
            if (!args.empty() && !parseNum(args[0], cap)) { std::cout << "bad cycle count\n"; continue; }
            bool hit = m.run(cap, breaks);
            if (hit) std::cout << "\nbreakpoint at " << hex(m.pc(), 4) << "\n";
            else std::cout << "\n[" << vm_status_str(m.status()) << "] pc=" << hex(m.pc(), 4)
                           << " cycles=" << m.cycles() << "\n";
        }
        else if (cmd == "regs" || cmd == "i") printRegs(m);
        else if (cmd == "mem") {
            if (args.empty() || !parseNum(args[0], a0)) { std::cout << "usage: mem <addr> [len]\n"; continue; }
            a1 = 64;
            if (args.size() > 1 && !parseNum(args[1], a1)) { std::cout << "bad length\n"; continue; }
            printMem(m, a0, a1);
        }
        else if (cmd == "dis") {
            a0 = m.pc(); a1 = 8;
            if (!args.empty() && !parseNum(args[0], a0)) { std::cout << "bad address\n"; continue; }
            if (args.size() > 1 && !parseNum(args[1], a1)) { std::cout << "bad count\n"; continue; }
            for (uint32_t i = 0; i < a1; i++) {
                uint32_t addr = a0 + 4 * i;
                std::cout << (addr == m.pc() ? "=> " : "   ") << hex(addr, 4) << ": " << m.disasm(addr) << "\n";
            }
        }
        else if (cmd == "break") {
            if (args.empty() || !parseNum(args[0], a0)) std::cout << "usage: break <addr>\n";
            else { breaks.insert(a0); std::cout << "breakpoint set at " << hex(a0, 4) << "\n"; }
        }
        else if (cmd == "unbreak") {
            if (args.empty() || !parseNum(args[0], a0)) std::cout << "usage: unbreak <addr>\n";
            else std::cout << (breaks.erase(a0) ? "removed\n" : "no such breakpoint\n");
        }
        else if (cmd == "breaks") {
            if (breaks.empty()) std::cout << "(none)\n";
            for (uint32_t b : breaks) std::cout << hex(b, 4) << "\n";
        }
        else std::cout << "unknown command '" << cmd << "' (try 'help')\n";
    }
    std::cout << "\n";
    return 0;
}
