#!/usr/bin/env python3
"""Regression coverage for shell32's checked-in SVG/ICO resources."""

import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
RESOURCES = ROOT / "dlls" / "shell32" / "resources"
BUILDIMAGE = ROOT / "tools" / "buildimage"
SIZES = (16, 20, 24, 32, 48)
STANDARD_ICONS = (
    "desktop",
    "folder",
    "folder_open",
    "mydocs",
    "nav_back",
    "nav_forward",
    "nav_up",
    "quick_computer",
    "quick_desktop",
    "quick_documents",
    "quick_downloads",
    "quick_home",
    "quick_pictures",
    "quick_videos",
    "users",
)


def ico_entries(data):
    if len(data) < 6:
        raise AssertionError("ICO header is truncated")
    reserved, kind, count = struct.unpack_from("<HHH", data)
    if (reserved, kind) != (0, 1):
        raise AssertionError("not an icon resource")
    expected_end = 6 + count * 16
    if len(data) < expected_end:
        raise AssertionError("ICO directory is truncated")

    entries = []
    for offset in range(6, expected_end, 16):
        width, height, colors, reserved, planes, depth, size, image_offset = (
            struct.unpack_from("<BBBBHHII", data, offset)
        )
        if image_offset + size > len(data):
            raise AssertionError("ICO image extends past the file")
        entries.append((width or 256, height or 256, planes, depth))
    return entries


class IconAssetsTests(unittest.TestCase):
    def test_checked_in_icons_have_standard_32_bit_entries(self):
        expected = [(size, size, 1, 32) for size in SIZES]
        for name in STANDARD_ICONS:
            with self.subTest(name=name):
                self.assertEqual(ico_entries((RESOURCES / f"{name}.ico").read_bytes()), expected)

        self.assertNotEqual(
            (RESOURCES / "users.ico").read_bytes(),
            (RESOURCES / "quick_home.ico").read_bytes(),
        )


class BuildImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rsvg = os.environ.get("RSVG") or shutil.which("rsvg-convert") or shutil.which("rsvg")
        cls.icotool = os.environ.get("ICOTOOL") or shutil.which("icotool")
        if not cls.rsvg or not cls.icotool:
            raise unittest.SkipTest("rsvg and icotool are required for icon regeneration")

    def generate_fixture(self):
        with tempfile.TemporaryDirectory(prefix="wine-shell-icons-") as directory:
            directory = Path(directory)
            source = directory / "fixture.svg"
            output = directory / "fixture.ico"
            source.write_text(
                '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1 1">'
                '<rect width="1" height="1" fill="#314159"/></svg>\n',
                encoding="utf-8",
            )
            environment = os.environ.copy()
            environment.update(
                RSVG=self.rsvg,
                ICOTOOL=self.icotool,
                CONVERT=str(directory / "convert-is-unavailable"),
                SOURCE_DATE_EPOCH="0",
            )
            subprocess.run(
                [str(BUILDIMAGE), str(source), str(output)],
                check=True,
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            data = output.read_bytes()
            return data, ico_entries(data)

    def test_standard_generation_works_without_convert_and_is_reproducible(self):
        first, entries = self.generate_fixture()
        second, second_entries = self.generate_fixture()
        self.assertEqual(entries, [(size, size, 1, 32) for size in SIZES])
        self.assertEqual(second_entries, entries)
        self.assertEqual(second, first)


if __name__ == "__main__":
    unittest.main()
