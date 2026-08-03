#!/usr/bin/env python3

import unittest
from pathlib import Path


class RunnerDefaultsTests(unittest.TestCase):
    def test_new_prefixes_disable_x11_window_manager_decorations(self):
        wine_root = Path(__file__).resolve().parents[3]
        wine_inf = (wine_root / "loader/wine.inf.in").read_text()
        expected = 'HKCU,"Software\\Wine\\X11 Driver","Decorated",2,"N"'
        self.assertIn(expected, wine_inf)
        # Flag 2 is FLG_ADDREG_NOCLOBBER: preserve an explicit user choice.
        self.assertIn("#define FLG_ADDREG_NOCLOBBER              0x00000002",
                      (wine_root / "include/setupapi.h").read_text())


if __name__ == "__main__":
    unittest.main()
