#!/usr/bin/env python3
"""Add Hebrew ranges and zero-width UI controls to Wine Tahoma.

This maintenance helper requires fontTools.  It is not run by the Wine build.
The generated tahoma.ttf and tahomabd.ttf remain checked-in build inputs.
"""

import argparse
import copy
import subprocess
import sys
import tempfile
from pathlib import Path

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._g_l_y_f import Glyph

HEBREW_RANGES = "U+0590-05FF,U+FB1D-FB4F"
HEBREW_REPRESENTATIVE_CODEPOINTS = (0x05B0, 0x05B4, 0x05D0, 0x05EA, 0x05F3, 0xFB2A)
PRESERVED_TABLES = ("BDF ", "FFTM", "VDMX")
OS2_COMPATIBILITY_METRICS = (
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
HHEA_COMPATIBILITY_METRICS = ("ascent", "descent", "lineGap")
SMOOTH_GASP_RANGES = {8: 2, 16: 3, 65535: 3}
EMBEDDED_BITMAP_TABLES = ("EBDT", "EBLC")
FORMAT_CONTROLS = (
    0x200B,
    0x200C,
    0x200D,
    0x200E,
    0x200F,
    0x202A,
    0x202B,
    0x202C,
    0x202D,
    0x202E,
    0x2060,
    0x2066,
    0x2067,
    0x2068,
    0x2069,
    0xFEFF,
)


def run_module(module, *args):
    subprocess.run([sys.executable, "-m", module, *map(str, args)], check=True)


def add_format_controls(font):
    glyph_order = font.getGlyphOrder()

    for codepoint in FORMAT_CONTROLS:
        name = f"uni{codepoint:04X}"
        if name not in glyph_order:
            glyph_order.append(name)
            font["glyf"][name] = Glyph()
            font["hmtx"][name] = (0, 0)

        for table in font["cmap"].tables:
            if table.isUnicode():
                table.cmap[codepoint] = name

    font.setGlyphOrder(glyph_order)
    font["maxp"].numGlyphs = len(glyph_order)


def unicode_cmap(font):
    """Return the union of all Unicode cmap subtables."""
    cmap = {}
    for table in font["cmap"].tables:
        if not table.isUnicode():
            continue
        for codepoint, glyph_name in table.cmap.items():
            if glyph_name != ".notdef" or codepoint not in cmap:
                cmap[codepoint] = glyph_name
    return cmap


def validate_merged_font(base, donor, merged):
    """Reject a successful-looking merge that lost coverage or metrics."""
    base_order = base.getGlyphOrder()
    if merged.getGlyphOrder()[: len(base_order)] != base_order:
        raise RuntimeError("fontTools changed the base glyph order")

    donor_cmap = unicode_cmap(donor)
    merged_cmap = unicode_cmap(merged)
    merged_glyphs = set(merged.getGlyphOrder())

    def has_glyph(codepoint):
        glyph_name = merged_cmap.get(codepoint)
        return glyph_name and glyph_name != ".notdef" and glyph_name in merged_glyphs

    expected = {
        codepoint
        for codepoint, glyph_name in donor_cmap.items()
        if glyph_name != ".notdef"
        and (0x0590 <= codepoint <= 0x05FF or 0xFB1D <= codepoint <= 0xFB4F)
    }
    missing = sorted(codepoint for codepoint in expected if not has_glyph(codepoint))
    if missing:
        formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in missing)
        raise RuntimeError(f"merged font lost Hebrew cmap entries: {formatted}")

    missing = [codepoint for codepoint in HEBREW_REPRESENTATIVE_CODEPOINTS if not has_glyph(codepoint)]
    if missing:
        formatted = ", ".join(f"U+{codepoint:04X}" for codepoint in missing)
        raise RuntimeError(f"merged font lacks representative Hebrew glyphs: {formatted}")

    for field in OS2_COMPATIBILITY_METRICS:
        if hasattr(base["OS/2"], field) and getattr(base["OS/2"], field) != getattr(merged["OS/2"], field):
            raise RuntimeError(f"merge changed OS/2 {field}")
    for field in HHEA_COMPATIBILITY_METRICS:
        if getattr(base["hhea"], field) != getattr(merged["hhea"], field):
            raise RuntimeError(f"merge changed hhea {field}")


def restore_compatibility_metrics(base, merged):
    """Keep Tahoma's global layout metrics while retaining donor coverage."""
    for field in OS2_COMPATIBILITY_METRICS:
        if hasattr(base["OS/2"], field):
            setattr(merged["OS/2"], field, getattr(base["OS/2"], field))
    for field in HHEA_COMPATIBILITY_METRICS:
        setattr(merged["hhea"], field, getattr(base["hhea"], field))


def build(base_path, donor_path, output_path, workdir):
    subset_path = workdir / (donor_path.stem + "-hebrew-subset.ttf")
    merged_path = workdir / (output_path.stem + "-merged.ttf")

    run_module(
        "fontTools.subset",
        donor_path,
        f"--unicodes={HEBREW_RANGES}",
        "--layout-features=*",
        "--glyph-names",
        "--name-IDs=*",
        f"--output-file={subset_path}",
    )
    run_module(
        "fontTools.merge",
        f"--output-file={merged_path}",
        base_path,
        subset_path,
    )

    base = TTFont(base_path, recalcTimestamp=False)
    donor = TTFont(donor_path, recalcTimestamp=False)
    merged = TTFont(merged_path, recalcTimestamp=False)

    # fontTools.merge intentionally drops these first-font-only tables.  The
    # appended glyphs do not participate in Tahoma's embedded bitmap strikes,
    # so preserving the original tables is valid and keeps legacy UI quality.
    for tag in PRESERVED_TABLES:
        if tag in base:
            merged[tag] = copy.deepcopy(base[tag])

    # The merge takes global line and average-width metrics from the donor.
    # That changes layout for every Latin Tahoma user. Preserve the original
    # compatibility metrics, while leaving merged Unicode/code-page coverage
    # and real horizontal glyph bounds intact for the appended Hebrew glyphs.
    restore_compatibility_metrics(base, merged)

    # Office paints some mixed-script Latin runs into monochrome masks.
    # Outline rasterization keeps those runs consistent with the appended
    # outline-only Hebrew glyphs; the legacy Latin bitmap strikes do not.
    for tag in EMBEDDED_BITMAP_TABLES:
        if tag in merged:
            del merged[tag]

    # Wine enables font smoothing by default. Keep hinting above 8 ppem, but
    # request grayscale outlines at every size instead of selecting the
    # monochrome embedded strikes used by legacy dialogs.
    merged["gasp"].gaspRange = SMOOTH_GASP_RANGES

    # Office UI strings include invisible bidi controls around mixed-script
    # text. Missing glyphs render as boxes when Segoe UI is substituted.
    add_format_controls(merged)
    validate_merged_font(base, donor, merged)
    merged["head"].created = base["head"].created
    merged["head"].modified = base["head"].modified
    merged.recalcTimestamp = False
    merged.save(output_path, reorderTables=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("base_regular", type=Path)
    parser.add_argument("base_bold", type=Path)
    parser.add_argument("donor_regular", type=Path)
    parser.add_argument("donor_bold", type=Path)
    parser.add_argument("output_regular", type=Path)
    parser.add_argument("output_bold", type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="tahoma-hebrew-") as directory:
        workdir = Path(directory)
        build(args.base_regular, args.donor_regular, args.output_regular, workdir)
        build(args.base_bold, args.donor_bold, args.output_bold, workdir)


if __name__ == "__main__":
    main()
