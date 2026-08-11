#!/usr/bin/env python3
"""Regression checks for the checked-in Hebrew Tahoma merge outputs."""

import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from fontTools.ttLib import TTFont


FONT_DIR = Path(__file__).resolve().parent
GENERATOR = FONT_DIR / "generate_tahoma_hebrew.py"
REGULAR = FONT_DIR / "tahoma.ttf"
BOLD = FONT_DIR / "tahomabd.ttf"
REPRESENTATIVE_CODEPOINTS = (0x05B0, 0x05B4, 0x05D0, 0x05EA, 0x05F3, 0xFB2A)
HEBREW_RANGE_COUNTS = ((0x0590, 0x05FF, 87), (0xFB1D, 0xFB4F, 46))
OS2_METRIC_FIELDS = (
    "xAvgCharWidth",
    "ySubscriptXSize",
    "ySubscriptYSize",
    "ySubscriptXOffset",
    "ySubscriptYOffset",
    "ySuperscriptXSize",
    "ySuperscriptYSize",
    "ySuperscriptXOffset",
    "ySuperscriptYOffset",
    "yStrikeoutSize",
    "yStrikeoutPosition",
    "sTypoAscender",
    "sTypoDescender",
    "sTypoLineGap",
    "usWinAscent",
    "usWinDescent",
    "sxHeight",
    "sCapHeight",
)
HHEA_METRIC_FIELDS = ("ascent", "descent", "lineGap")
EXPECTED_OS2_METRICS = {
    "tahoma.ttf": {
        "xAvgCharWidth": 910,
        "ySubscriptXSize": 1434,
        "ySubscriptYSize": 1331,
        "ySubscriptXOffset": 0,
        "ySubscriptYOffset": 293,
        "ySuperscriptXSize": 1434,
        "ySuperscriptYSize": 1331,
        "ySuperscriptXOffset": 0,
        "ySuperscriptYOffset": 928,
        "yStrikeoutSize": 130,
        "yStrikeoutPosition": 689,
        "sTypoAscender": 1566,
        "sTypoDescender": -423,
        "sTypoLineGap": 59,
        "usWinAscent": 2049,
        "usWinDescent": 423,
        "sxHeight": 1120,
        "sCapHeight": 1489,
    },
    "tahomabd.ttf": {
        "xAvgCharWidth": 1036,
        "ySubscriptXSize": 1434,
        "ySubscriptYSize": 1331,
        "ySubscriptXOffset": 0,
        "ySubscriptYOffset": 293,
        "ySuperscriptXSize": 1434,
        "ySuperscriptYSize": 1331,
        "ySuperscriptXOffset": 0,
        "ySuperscriptYOffset": 928,
        "yStrikeoutSize": 130,
        "yStrikeoutPosition": 689,
        "sTypoAscender": 1566,
        "sTypoDescender": -423,
        "sTypoLineGap": 59,
        "usWinAscent": 2049,
        "usWinDescent": 423,
        "sxHeight": 1120,
        "sCapHeight": 1493,
    },
}
EXPECTED_SHA256 = {
    "tahoma.ttf": "55250260837de53e58e97a7592c13760c7612b9761abeff1e6468c907efca366",
    "tahomabd.ttf": "e3a09d985ed5abf7593bf921046c60afbbd8de6770729e2f6ce64017e3abcc8f",
}
EXPECTED_HHEA_METRICS = {"ascent": 2049, "descent": -423, "lineGap": 0}


def unicode_cmap(font):
    cmap = {}
    for table in font["cmap"].tables:
        if not table.isUnicode():
            continue
        for codepoint, glyph_name in table.cmap.items():
            if glyph_name != ".notdef" or codepoint not in cmap:
                cmap[codepoint] = glyph_name
    return cmap


class TahomaHebrewFontTests(unittest.TestCase):
    def assert_font_contract(self, path):
        font = TTFont(path, recalcTimestamp=False)
        glyph_order = set(font.getGlyphOrder())
        cmap = unicode_cmap(font)
        for first, last, expected in HEBREW_RANGE_COUNTS:
            with self.subTest(path=path.name, range=f"U+{first:04X}-U+{last:04X}"):
                actual = sum(first <= codepoint <= last for codepoint in cmap)
                self.assertEqual(actual, expected)
        for codepoint in REPRESENTATIVE_CODEPOINTS:
            with self.subTest(path=path.name, codepoint=f"U+{codepoint:04X}"):
                self.assertIn(codepoint, cmap)
                self.assertNotEqual(cmap[codepoint], ".notdef")
                self.assertIn(cmap[codepoint], glyph_order)

    def assert_metrics_equal(self, base_path, merged_path):
        base = TTFont(base_path, recalcTimestamp=False)
        merged = TTFont(merged_path, recalcTimestamp=False)
        for field in OS2_METRIC_FIELDS:
            with self.subTest(table="OS/2", field=field):
                self.assertEqual(getattr(base["OS/2"], field), getattr(merged["OS/2"], field))
        for field in HHEA_METRIC_FIELDS:
            with self.subTest(table="hhea", field=field):
                self.assertEqual(getattr(base["hhea"], field), getattr(merged["hhea"], field))

    def test_checked_in_fonts_have_hebrew_and_tahoma_metrics(self):
        for path in (REGULAR, BOLD):
            self.assert_font_contract(path)
            with self.subTest(path=path.name, property="sha256"):
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), EXPECTED_SHA256[path.name])
            expected_os2 = EXPECTED_OS2_METRICS[path.name]
            font = TTFont(path, recalcTimestamp=False)
            for field, expected in expected_os2.items():
                with self.subTest(path=path.name, table="OS/2", field=field):
                    self.assertEqual(getattr(font["OS/2"], field), expected)
            for field, expected in EXPECTED_HHEA_METRICS.items():
                with self.subTest(path=path.name, table="hhea", field=field):
                    self.assertEqual(getattr(font["hhea"], field), expected)

    def test_regeneration_is_byte_reproducible_and_preserves_metrics(self):
        inputs = (
            Path(os.environ.get("TAHOMA_BASE_REGULAR", REGULAR)),
            Path(os.environ.get("TAHOMA_BASE_BOLD", BOLD)),
            Path(os.environ.get("TAHOMA_DONOR_REGULAR", REGULAR)),
            Path(os.environ.get("TAHOMA_DONOR_BOLD", BOLD)),
        )
        with tempfile.TemporaryDirectory(prefix="tahoma-hebrew-test-") as directory:
            directory = Path(directory)
            outputs = []
            for run in (1, 2):
                run_outputs = (directory / f"regular-{run}.ttf", directory / f"bold-{run}.ttf")
                subprocess.run(
                    [
                        sys.executable,
                        str(GENERATOR),
                        *(str(path) for path in inputs),
                        *(str(path) for path in run_outputs),
                    ],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    text=True,
                )
                outputs.append(run_outputs)

            self.assertEqual(outputs[0][0].read_bytes(), outputs[1][0].read_bytes())
            self.assertEqual(outputs[0][1].read_bytes(), outputs[1][1].read_bytes())
            for base, output in zip(inputs[:2], outputs[0]):
                self.assert_font_contract(output)
                self.assert_metrics_equal(base, output)

    def test_sfd_sources_declare_checked_in_merge_outputs(self):
        for source in (FONT_DIR / "tahoma.sfd", FONT_DIR / "tahomabd.sfd"):
            with self.subTest(source=source.name):
                self.assertIn("#pragma makedep install external-ttf", source.read_text())


if __name__ == "__main__":
    unittest.main()
