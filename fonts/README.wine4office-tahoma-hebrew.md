# Wine4Office Hebrew additions to Tahoma

Wine's metric-compatible Tahoma files did not contain Hebrew glyphs.  Some
Office UI paths ask a Tahoma `IDWriteFontFace` for glyph indices directly, so
GDI `FontSubstitutes` and normal DirectWrite fallback cannot repair those
paths.  The checked-in `tahoma.ttf` and `tahomabd.ttf` therefore retain the
original Wine Tahoma faces and append the Hebrew ranges:

- U+0590-U+05FF
- U+FB1D-U+FB4F

They also provide zero-width glyphs for the Unicode format controls used by
mixed-direction Office UI strings:

- U+200B-U+200F
- U+202A-U+202E
- U+2060 and U+2066-U+2069
- U+FEFF

The appended outlines and Hebrew OpenType layout data come from Liberation
Sans.  See `LICENSE.Liberation`.  The primary family remains Tahoma, not the
reserved Liberation name.

## Reproduction

The current files were generated with:

- Wine base fonts from commit `43c31c5a2ba73810f87cbb669f8d7c9a39b30680`
- Debian `fonts-liberation2` `1:2.1.5-3`
- fontTools commit `386243ed95d6a42114f7695f21de3ef45524108a`
- Liberation donor SHA-256:
  - regular: `bade59d822652f76e6941aa87b40a87c13d1cc70db98ededb5011127efafd1d3`
  - bold: `1b5f2da6f4cadce4c05b9ecebe3a6fcd374eb95ae443605e799f4c3287978939`

Example, from the Wine source root:

```sh
git show 43c31c5a2ba73810f87cbb669f8d7c9a39b30680:fonts/tahoma.ttf > /tmp/tahoma-base.ttf
git show 43c31c5a2ba73810f87cbb669f8d7c9a39b30680:fonts/tahomabd.ttf > /tmp/tahomabd-base.ttf
python3 fonts/generate_tahoma_hebrew.py \
  /tmp/tahoma-base.ttf /tmp/tahomabd-base.ttf \
  /path/to/LiberationSans-Regular.ttf \
  /path/to/LiberationSans-Bold.ttf \
  fonts/tahoma.ttf fonts/tahomabd.ttf
```

Expected output SHA-256:

- `tahoma.ttf`: `55250260837de53e58e97a7592c13760c7612b9761abeff1e6468c907efca366`
- `tahomabd.ttf`: `e3a09d985ed5abf7593bf921046c60afbbd8de6770729e2f6ce64017e3abcc8f`

## Validation

Run the focused regression from the source root:

```sh
python3 -m unittest fonts/test_tahoma_hebrew.py
```

The test checks representative Hebrew cmap entries, every OS/2 and hhea
compatibility metric, the two generated files' byte-for-byte reproducibility,
and the `external-ttf` maintainer directive. To compare a regeneration with
the original base and donor files, set `TAHOMA_BASE_REGULAR`,
`TAHOMA_BASE_BOLD`, `TAHOMA_DONOR_REGULAR`, and `TAHOMA_DONOR_BOLD` before
running the test.

The generator restores Tahoma's original BDF, FFTM, and VDMX tables after
fontTools merges the outline and layout tables, but deliberately omits the
EBDT and EBLC embedded-bitmap tables. Office paints some mixed-script UI runs
into monochrome masks; using legacy bitmap strikes for Latin while Hebrew is
outline-only makes the scripts visibly inconsistent. Omitting the strikes
keeps both scripts on the same outline rasterizer.

It also restores Tahoma's original global OS/2 and hhea compatibility metrics.
The merge retains Hebrew Unicode/code-page coverage and actual appended-glyph
bounds, but must not change line spacing or average width for existing Latin
Tahoma users. The SFD files are marked `external-ttf` because this documented
merge pipeline, rather than plain FontForge SFD generation, owns the checked-in
TTF files.

The `gasp` ranges request smoothing at every size (`GASP_DOGRAY`) and retain
grid fitting above 8 ppem. The format controls have no outlines and zero
advance width, preventing invisible bidi marks from rendering as missing
glyph boxes.
