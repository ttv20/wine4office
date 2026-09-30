#!/usr/bin/env python3
"""Test the package gate's failure handling without needing Wine or a GPU."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


CONTRACT = Path(__file__).resolve().parents[1]


class OfficeRuntimeGateTest(unittest.TestCase):
    def setUp(self):
        # Retain task-owned fixtures and logs for diagnosis rather than deleting them.
        self.root = Path(tempfile.mkdtemp(prefix="wine4office-runtime-gate-"))
        self.runner = self.root / "runner"
        self.bin = self.runner / "bin"
        self.bin.mkdir(parents=True)
        for arch in ("i386", "x86_64"):
            directory = self.runner / "lib/wine" / f"{arch}-windows"
            directory.mkdir(parents=True)
            for name in ("windows.applicationmodel.dll", "twinapi.appcore.dll",
                         "windows.security.enterprisedata.dll",
                         "windows.security.authentication.onlineid.dll", "windows.ui.dll",
                         "dcomp.dll", "wine4officeauth.exe"):
                (directory / name).touch()
        self.executable("wine", r"""#!/usr/bin/env python3
import os, pathlib, re, sys
if sys.argv[1] == 'wineboot':
    pathlib.Path(os.environ['WINEPREFIX']).mkdir()
    sys.exit(int(os.environ.get('BOOT_EXIT', '0')))
bits = re.search(r'-(32|64)\.exe$', sys.argv[1]).group(1)
if os.environ.get('WRONG_BITS'):
    bits = '32' if bits == '64' else '64'
if not os.environ.get('EMPTY_OUTPUT'):
    profile = 'soda' if '--soda' in sys.argv else 'supported'
    print(f'office_runtime:{profile}:{bits}:passed')
sys.exit(int(os.environ.get('PROBE_EXIT', '0')))
""")
        self.executable("wineserver", "#!/bin/sh\nexit 0\n")
        compiler = """#!/usr/bin/env python3
import os, pathlib, sys
if os.environ.get('FAIL_COMPILER') == pathlib.Path(sys.argv[0]).name:
    print('intentional compiler failure', file=sys.stderr)
    sys.exit(8)
pathlib.Path(sys.argv[sys.argv.index('-o') + 1]).touch()
"""
        for name in ("x86_64-w64-mingw32-gcc", "i686-w64-mingw32-gcc"):
            self.executable(name, compiler)
        self.results = self.root / "results"

    def executable(self, name, contents):
        path = self.bin / name
        path.write_text(contents)
        path.chmod(0o755)

    def run_gate(self, *args, **overrides):
        env = os.environ.copy()
        env["PATH"] = f"{self.bin}{os.pathsep}{env['PATH']}"
        env.update(overrides)
        result = subprocess.run([str(CONTRACT / "check-office-runtime.sh"), str(self.runner),
                                 str(self.results), *args], env=env, capture_output=True,
                                text=True, timeout=15)
        (self.root / "gate.log").write_text(result.stdout + result.stderr)
        return result

    def test_both_architectures_are_executed(self):
        result = self.run_gate()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for bits in (32, 64):
            self.assertTrue((self.results / f"prefix-{bits}").is_dir())
            self.assertIn(f"supported:{bits}:passed", (self.results / f"runtime-{bits}.log").read_text())

    def test_missing_architecture_module_fails_before_prefix_creation(self):
        module = self.runner / "lib/wine/i386-windows/wine4officeauth.exe"
        module.rename(module.with_suffix(".disabled"))
        result = self.run_gate()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("i386-windows/wine4officeauth.exe", result.stderr)
        self.assertIn("i386-windows/wine4officeauth.exe",
                      (self.results / "preflight.log").read_text())
        self.assertFalse((self.results / "prefix-64").exists())

    def test_missing_tool_leaves_preflight_evidence(self):
        compiler = self.bin / "i686-w64-mingw32-gcc"
        compiler.rename(compiler.with_suffix(".disabled"))
        # Exclude system compilers so the fixture also works on MinGW hosts.
        for name in ("bash", "python3", "dirname", "mkdir", "timeout", "cat"):
            (self.bin / name).symlink_to(shutil.which(name))
        result = self.run_gate(PATH=str(self.bin))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("i686-w64-mingw32-gcc", (self.results / "preflight.log").read_text())

    def test_compilation_failure_still_checks_other_architecture(self):
        result = self.run_gate(FAIL_COMPILER="x86_64-w64-mingw32-gcc")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("intentional compiler failure", (self.results / "compile-64.log").read_text())
        self.assertIn("supported:32:passed", (self.results / "runtime-32.log").read_text())
        self.assertIn("Office runtime validation failed", result.stderr)

    def test_environment_initialization_failure_is_not_a_product_pass(self):
        result = self.run_gate(BOOT_EXIT="9")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("failed to initialize", result.stderr)
        self.assertFalse((self.results / "runtime-64.log").exists())

    def test_nonzero_exit_is_rejected_even_with_a_pass_marker(self):
        result = self.run_gate(PROBE_EXIT="9")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exit 9", result.stderr)

    def test_empty_successful_process_is_rejected(self):
        result = self.run_gate(EMPTY_OUTPUT="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("did not report a complete", result.stderr)

    def test_wrong_process_architecture_is_rejected(self):
        result = self.run_gate(WRONG_BITS="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("did not report a complete", result.stderr)

    def test_existing_result_directory_is_preserved(self):
        self.results.mkdir()
        evidence = self.results / "previous.log"
        evidence.write_text("earlier evidence")
        result = self.run_gate()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(evidence.read_text(), "earlier evidence")

    def test_soda_profile_reaches_both_processes(self):
        result = self.run_gate("--soda")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for bits in (32, 64):
            self.assertIn(f"soda:{bits}:passed", (self.results / f"runtime-{bits}.log").read_text())


if __name__ == "__main__":
    unittest.main()
