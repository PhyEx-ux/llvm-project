#!/usr/bin/env python3
"""不依赖目标工具链的验收器回归：完整成功、失败、超时与旧输出。"""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RUNNER = Path(__file__).resolve().with_name("run-qemu.py")
GOOD = "".join(f"feature{i}:OK\n" for i in range(14)) + "DEMO-PASS\n"


class RunnerTest(unittest.TestCase):
    def run_case(self, actual, expected=GOOD, hang=False, stale=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fake = root / "qemu"
            fake.write_text("#!/usr/bin/env python3\nimport sys, time\n"
                            f"sys.stdout.write({actual!r}); sys.stdout.flush()\n"
                            + ("time.sleep(30)\n" if hang else ""))
            fake.chmod(0o755)
            oracle = root / "expected"
            oracle.write_text(expected)
            serial = root / "output.serial"
            if stale:
                serial.write_text(GOOD)
            result = subprocess.run(
                [sys.executable, str(RUNNER), "--qemu", str(fake),
                 "--machine", "test-machine", "--image", str(root / "unused.hex"),
                 "--serial", str(serial),
                 "--expected", str(oracle), "--timeout", "0.5"],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)
            return result, serial.read_text() if serial.exists() else None

    def test_complete_success(self):
        result, serial = self.run_case(GOOD, hang=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(serial, GOOD)
        self.assertIn(b"DEMO-CHECK-PASS", result.stdout)

    def test_equal_failure_is_not_success(self):
        bad = GOOD.replace("feature0:OK", "feature0:FAIL got=1 expect=0")
        result, _ = self.run_case(bad, bad)
        self.assertNotEqual(result.returncode, 0)

    def test_missing_checkpoint(self):
        short = GOOD.replace("feature0:OK\n", "")
        result, _ = self.run_case(short, short)
        self.assertNotEqual(result.returncode, 0)

    def test_missing_newline(self):
        result, _ = self.run_case(GOOD.rstrip("\n"))
        self.assertNotEqual(result.returncode, 0)

    def test_mismatch(self):
        result, _ = self.run_case(GOOD.replace("feature0", "different"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"DEMO-CHECK-FAIL", result.stderr)

    def test_early_exit_cannot_reuse_stale_output(self):
        result, serial = self.run_case("", stale=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(serial, "")

    def test_timeout_cannot_reuse_stale_output(self):
        result, serial = self.run_case("feature0:OK\n", hang=True, stale=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(serial, "feature0:OK\n")

    def test_extra_output_is_not_filtered(self):
        result, serial = self.run_case(GOOD + "unexpected\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(serial.endswith("unexpected\n"))


if __name__ == "__main__":
    unittest.main()
