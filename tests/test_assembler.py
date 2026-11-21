import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from asm import AsmError, assemble  # noqa: E402


def words(image):
    return [int.from_bytes(image[i:i + 4], "little") for i in range(0, len(image), 4)]


class AssemblerTests(unittest.TestCase):
    def test_halt_is_little_endian_with_opcode_in_top_byte(self):
        self.assertEqual(assemble("HALT"), bytes([0x00, 0x00, 0x00, 0x01]))

    def test_register_and_immediate_fields(self):
        self.assertEqual(words(assemble("LDI r3, 0x1234")), [0x03_3_0_1234])
        self.assertEqual(words(assemble("ADD r1, r2, r3")), [0x10_1_2_3_000])
        self.assertEqual(words(assemble("ADDI r1, r2, -1")), [0x1A_1_2_FFFF])
        self.assertEqual(words(assemble("CMP r4, r5")), [0x20_0_4_5_000])

    def test_sp_alias_and_case_insensitive_mnemonics(self):
        self.assertEqual(assemble("push sp"), assemble("PUSH r15"))

    def test_memory_operands(self):
        self.assertEqual(words(assemble("LD r1, [r2]")), [0x05_1_2_0000])
        self.assertEqual(words(assemble("LD r1, [r2+8]")), [0x05_1_2_0008])
        self.assertEqual(words(assemble("ST r1, [r2-4]")), [0x06_1_2_FFFC])

    def test_forward_and_backward_labels(self):
        img = assemble("start: JMP end\nNOP\nend: JMP start")
        self.assertEqual(words(img), [0x30_000008, 0x00, 0x30_000000])

    def test_equ_and_expressions(self):
        img = assemble(".equ BASE, 0x10\nLDI r1, BASE+4\nLDI r2, BASE-1")
        self.assertEqual(words(img), [0x03_1_0_0014, 0x03_2_0_000F])

    def test_data_directives(self):
        img = assemble(".word 1, -1\n.byte 7, 'A'\n.string \"hi\\n\"")
        self.assertEqual(img, b"\x01\0\0\0\xff\xff\xff\xff" + b"\x07A" + b"hi\n\0")

    def test_comment_char_inside_string_and_char_literal(self):
        img = assemble('.string "a;b" ; real comment\n.byte \';\'')
        self.assertEqual(img, b"a;b\0;")

    def test_org_and_align(self):
        img = assemble(".byte 1\n.align 4\nHALT\n.org 12\nNOP")
        self.assertEqual(len(img), 16)
        self.assertEqual(img[4:8], bytes([0, 0, 0, 1]))

    def test_errors(self):
        for src in ("FOO r1", "LDI r16, 1", "LDI r1, 70000", "JMP nowhere",
                    "x: NOP\nx: NOP", ".byte 1\nNOP", "ADD r1, r2", "LD r1, r2",
                    ".bogus 1", "JMP 70000"):
            with self.subTest(src=src), self.assertRaises(AsmError):
                assemble(src)

    def test_error_reports_line_number(self):
        with self.assertRaisesRegex(AsmError, "line 3"):
            assemble("NOP\nNOP\nBAD")


if __name__ == "__main__":
    unittest.main()
