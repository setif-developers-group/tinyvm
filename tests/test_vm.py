import unittest

from vmlib import (ERR_DIV_ZERO, ERR_MEM, ERR_OPCODE, FLAG_C, FLAG_N, FLAG_V, FLAG_Z,
                   HALTED, OK, VM, disasm, run_asm)


class ArithmeticTests(unittest.TestCase):
    def test_ldi_sign_extends(self):
        vm = run_asm("LDI r1, -2\nLDI r2, 0x7FFF\nHALT")
        self.assertEqual(vm.reg(1), 0xFFFFFFFE)
        self.assertEqual(vm.reg(2), 0x7FFF)
        self.assertEqual(vm.status, HALTED)

    def test_add_sub_wraparound(self):
        vm = run_asm("LDI r1, -1\nLDI r2, 1\nADD r3, r1, r2\nSUB r4, r2, r1\nSUB r5, r3, r2\nHALT")
        self.assertEqual(vm.reg(3), 0)
        self.assertEqual(vm.reg(4), 2)
        self.assertEqual(vm.reg(5), 0xFFFFFFFF)

    def test_mul_wraps_to_32_bits(self):
        vm = run_asm("LDI r1, 0x7FFF\nSHL r1, r1, r1\nMUL r2, r1, r1\nHALT")
        self.assertEqual(vm.reg(2), (vm.reg(1) * vm.reg(1)) & 0xFFFFFFFF)

    def test_signed_div_mod_truncate_toward_zero(self):
        vm = run_asm("LDI r1, -7\nLDI r2, 2\nDIV r3, r1, r2\nMOD r4, r1, r2\nHALT")
        self.assertEqual(vm.sreg(3), -3)
        self.assertEqual(vm.sreg(4), -1)

    def test_div_by_zero_faults_and_keeps_pc(self):
        vm = run_asm("LDI r1, 5\nLDI r2, 0\nDIV r3, r1, r2\nHALT")
        self.assertEqual(vm.status, ERR_DIV_ZERO)
        self.assertEqual(vm.pc, 8)

    def test_int_min_div_minus_one_does_not_trap(self):
        vm = VM()
        from vmlib import assemble
        vm.load(assemble("DIV r3, r1, r2\nMOD r4, r1, r2\nHALT"))
        vm.set_reg(1, 0x80000000)
        vm.set_reg(2, 0xFFFFFFFF)
        vm.run()
        self.assertEqual(vm.status, HALTED)
        self.assertEqual(vm.reg(3), 0x80000000)
        self.assertEqual(vm.reg(4), 0)

    def test_logic_and_shifts(self):
        vm = run_asm("""LDI r1, 0x0F0F
                        LDI r2, 0x00FF
                        AND r3, r1, r2
                        OR  r4, r1, r2
                        XOR r5, r1, r2
                        NOT r6, r2
                        LDI r7, 4
                        SHL r8, r2, r7
                        SHR r9, r2, r7
                        HALT""")
        self.assertEqual([vm.reg(i) for i in (3, 4, 5, 6, 8, 9)],
                         [0x000F, 0x0FFF, 0x0FF0, 0xFFFFFF00, 0x0FF0, 0x000F])

    def test_shr_is_logical_and_shift_amount_masked(self):
        vm = run_asm("LDI r1, -1\nLDI r2, 28\nSHR r3, r1, r2\nLDI r4, 33\nLDI r5, 1\nSHL r6, r5, r4\nHALT")
        self.assertEqual(vm.reg(3), 0xF)
        self.assertEqual(vm.reg(6), 2)  # 33 & 31 == 1

    def test_addi_negative(self):
        vm = run_asm("LDI r1, 10\nADDI r1, r1, -3\nHALT")
        self.assertEqual(vm.reg(1), 7)

    def test_mov_and_sp_initial_value(self):
        vm = run_asm("MOV r1, sp\nHALT")
        self.assertEqual(vm.reg(1), 0x10000)


class MemoryTests(unittest.TestCase):
    def test_word_store_load_is_little_endian(self):
        vm = run_asm("LDI r1, 0x1234\nLDI r2, 100\nST r1, [r2]\nLD r3, [r2]\nHALT")
        self.assertEqual(vm.mem(100, 4), bytes([0x34, 0x12, 0, 0]))
        self.assertEqual(vm.reg(3), 0x1234)

    def test_byte_access_and_offsets(self):
        vm = run_asm("LDI r1, 0x1FF\nLDI r2, 200\nSTB r1, [r2+3]\nLDB r3, [r2+3]\nLDB r4, [r2+2]\nHALT")
        self.assertEqual(vm.reg(3), 0xFF)
        self.assertEqual(vm.reg(4), 0)

    def test_unaligned_word_access_allowed(self):
        vm = run_asm("LDI r1, 7\nLDI r2, 101\nST r1, [r2]\nLD r3, [r2]\nHALT")
        self.assertEqual(vm.reg(3), 7)

    def test_out_of_range_access_faults(self):
        for body in ("LD r1, [r2]", "ST r1, [r2]", "LDB r1, [r2]", "STB r1, [r2]"):
            with self.subTest(body=body):
                vm = VM()
                from vmlib import assemble
                vm.load(assemble(body + "\nHALT"))
                vm.set_reg(2, 0xFFFD if body.startswith(("LD ", "ST ")) else 0x10000)
                vm.run()
                self.assertEqual(vm.status, ERR_MEM)

    def test_load_rejects_oversized_image(self):
        vm = VM()
        self.assertEqual(vm.load(b"\0" * 10, 65530), -1)
        self.assertEqual(vm.load(b"\0" * 6, 65530), 0)


class ControlFlowTests(unittest.TestCase):
    def cmp_flags(self, a, b):
        vm = VM()
        from vmlib import assemble
        vm.load(assemble("CMP r1, r2\nHALT"))
        vm.set_reg(1, a & 0xFFFFFFFF)
        vm.set_reg(2, b & 0xFFFFFFFF)
        vm.run()
        return vm.flags

    def test_cmp_flags(self):
        self.assertEqual(self.cmp_flags(5, 5), FLAG_Z)
        self.assertEqual(self.cmp_flags(1, 2), FLAG_N | FLAG_C)
        self.assertEqual(self.cmp_flags(2, 1), 0)
        self.assertEqual(self.cmp_flags(0x80000000, 1), FLAG_V)        # signed overflow
        self.assertEqual(self.cmp_flags(1, 0x80000000), FLAG_N | FLAG_C | FLAG_V)

    def branch_taken(self, jump, a, b):
        vm = VM()
        from vmlib import assemble
        vm.load(assemble(f"CMP r1, r2\n{jump} taken\nLDI r3, 0\nHALT\ntaken: LDI r3, 1\nHALT"))
        vm.set_reg(1, a & 0xFFFFFFFF)
        vm.set_reg(2, b & 0xFFFFFFFF)
        vm.run()
        return vm.reg(3) == 1

    def test_conditional_jumps_are_signed(self):
        cases = [("JZ", 3, 3, True), ("JZ", 3, 4, False), ("JNZ", 3, 4, True),
                 ("JLT", -1, 1, True), ("JLT", 1, -1, False), ("JLT", 3, 3, False),
                 ("JGT", 1, -1, True), ("JGT", 3, 3, False),
                 ("JLE", 3, 3, True), ("JLE", -5, -4, True), ("JLE", 4, 3, False),
                 ("JGE", 3, 3, True), ("JGE", 1, -1, True), ("JGE", -1, 1, False),
                 ("JLT", -0x80000000, 1, True), ("JGT", 0x7FFFFFFF, -1, True)]
        for j, a, b, expected in cases:
            with self.subTest(jump=j, a=a, b=b):
                self.assertEqual(self.branch_taken(j, a, b), expected)

    def test_loop_counts_down(self):
        vm = run_asm("LDI r1, 5\nLDI r2, 0\nl: ADDI r2, r2, 2\nADDI r1, r1, -1\nCMPI r1, 0\nJNZ l\nHALT")
        self.assertEqual(vm.reg(2), 10)

    def test_call_ret_push_pop(self):
        vm = run_asm("""LDI r1, 7
                        CALL f
                        PUSH r1
                        POP r5
                        HALT
                  f:    ADDI r1, r1, 1
                        RET""")
        self.assertEqual(vm.reg(1), 8)
        self.assertEqual(vm.reg(5), 8)
        self.assertEqual(vm.reg(15), 0x10000)  # stack balanced

    def test_call_pushes_return_address(self):
        vm = VM()
        from vmlib import assemble
        vm.load(assemble("CALL f\nHALT\nf: NOP\nHALT"))
        vm.step()
        self.assertEqual(vm.reg(15), 0xFFFC)
        self.assertEqual(vm.word(0xFFFC), 4)
        self.assertEqual(vm.pc, 8)

    def test_stack_underflow_and_overflow_fault(self):
        self.assertEqual(run_asm("POP r1\nHALT").status, ERR_MEM)
        self.assertEqual(run_asm("RET").status, ERR_MEM)
        vm = VM()
        from vmlib import assemble
        vm.load(assemble("PUSH r1\nHALT"))
        vm.set_reg(15, 2)  # no room left below the stack pointer
        vm.run()
        self.assertEqual(vm.status, ERR_MEM)
        self.assertEqual(vm.reg(15), 2)  # SP untouched by the failed push


class MachineTests(unittest.TestCase):
    def test_output_instructions(self):
        vm = run_asm("LDI r1, -42\nOUT r1\nLDI r2, 0x41\nOUTC r2\nOUTC r2\nHALT")
        self.assertEqual(vm.output(), "-42\nAA")
        self.assertEqual(vm.output(), "")  # buffer was drained

    def test_illegal_opcode(self):
        vm = VM()
        vm.load(bytes([0, 0, 0, 0xFF]))
        self.assertEqual(vm.step(), ERR_OPCODE)

    def test_pc_running_off_memory_faults(self):
        vm = VM()
        vm.load(bytes([0, 0, 0, 0x30, 0xFC, 0xFF]), 0)  # NOP (JMP form irrelevant); fill below
        vm.load((0x30_00FFFC).to_bytes(4, "little"), 0)  # JMP 0xFFFC -> then NOP runs past end
        vm.run(10)
        self.assertEqual(vm.status, ERR_MEM)

    def test_halt_is_sticky_and_cycles_counted(self):
        vm = run_asm("NOP\nNOP\nHALT\nNOP")
        self.assertEqual(vm.status, HALTED)
        self.assertEqual(vm.cycles, 3)
        self.assertEqual(vm.step(), HALTED)
        self.assertEqual(vm.cycles, 3)

    def test_run_respects_cycle_budget(self):
        vm = VM()
        vm.load((0x30_000000).to_bytes(4, "little"))  # JMP 0 forever
        self.assertEqual(vm.run(100), OK)
        self.assertEqual(vm.cycles, 100)

    def test_reset_clears_state(self):
        vm = run_asm("LDI r1, 9\nHALT")
        vm.reset()
        self.assertEqual((vm.reg(1), vm.pc, vm.status, vm.cycles, vm.reg(15)), (0, 0, OK, 0, 0x10000))
        self.assertEqual(vm.mem(0, 4), b"\0\0\0\0")

    def test_disassembler(self):
        from vmlib import assemble
        cases = ["NOP", "HALT", "LDI r1, -5", "ADD r1, r2, r3", "ADDI r1, r2, 4", "CMP r1, r2",
                 "CMPI r3, 10", "LD r1, [r2]", "ST r1, [r2+8]", "LDB r1, [r2-1]",
                 "PUSH sp", "OUTC r4", "JMP 0x0010", "CALL 0x0020", "RET", "NOT r1, r2"]
        for text in cases:
            with self.subTest(text=text):
                word = int.from_bytes(assemble(text), "little")
                self.assertEqual(" ".join(disasm(word).split()), text)


if __name__ == "__main__":
    unittest.main()
