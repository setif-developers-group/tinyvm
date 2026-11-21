"""Run every examples/*.asm and compare against its .out file (the conformance suite)."""
import unittest
from pathlib import Path

from vmlib import HALTED, ROOT, VM, assemble

EXAMPLES = sorted((ROOT / "examples").glob("*.asm"))


class ExampleProgramTests(unittest.TestCase):
    def test_examples_exist(self):
        self.assertTrue(EXAMPLES)

    def test_all_examples_produce_expected_output(self):
        for asm in EXAMPLES:
            expected = asm.with_suffix(".out")
            if not expected.exists():
                continue
            with self.subTest(example=asm.name):
                vm = VM()
                vm.load(assemble(asm.read_text()))
                vm.run(1_000_000)
                self.assertEqual(vm.status, HALTED)
                self.assertEqual(vm.output(), expected.read_text())


if __name__ == "__main__":
    unittest.main()
