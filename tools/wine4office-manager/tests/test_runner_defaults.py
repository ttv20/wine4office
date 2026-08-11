#!/usr/bin/env python3

import unittest
from pathlib import Path


class RunnerDefaultsTests(unittest.TestCase):
    def test_new_prefixes_keep_upstream_office_x11_defaults(self):
        wine_root = Path(__file__).resolve().parents[3]
        wine_inf = (wine_root / "loader/wine.inf.in").read_text()
        office_defaults = (
            'Software\\Wine\\AppDefaults\\WINWORD.EXE\\X11 Driver',
            'Software\\Wine\\AppDefaults\\EXCEL.EXE\\X11 Driver',
            'Software\\Wine\\AppDefaults\\POWERPNT.EXE\\X11 Driver',
            'Software\\Wine\\AppDefaults\\OUTLOOK.EXE\\X11 Driver',
        )
        for default in office_defaults:
            self.assertNotIn(default, wine_inf)
        self.assertNotIn('Software\\Wine\\X11 Driver","Decorated"', wine_inf)
        self.assertNotIn('Software\\Wine\\X11 Driver","UseXVidMode"', wine_inf)
        self.assertNotIn('Software\\Wine\\AppDefaults\\', wine_inf)


if __name__ == "__main__":
    unittest.main()
