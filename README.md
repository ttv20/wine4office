# Wine4Office

Wine4Office is an experimental [Wine](https://www.winehq.org/) fork for running
modern Microsoft Word and Excel on Linux without a Windows virtual machine.

> Every Wine4Office investigation, code change, test, and document was produced with
> AI, primarily **ChatGPT-5.6-sol** and **Grok 4.5**. I directed and tested the
> work, but **I do not know C** and cannot independently audit the C/C++ code.

## Important

- **Word and Excel were tested. PowerPoint and Outlook open, but were not
  functionally tested.**
- Tests ran only on **7th-generation and 12th-generation Intel hardware**.
- Licensing was tested only with an **organizational Microsoft 365 subscription**.
  KMS, MAK, VL, perpetual, retail, device, shared-computer, and other licensing
  methods remain untested.

This is a narrow experiment, not a promise that Wine4Office will work with another
machine, Office edition, account, or Wine prefix.

## What worked

In the tested installation, Word can:

- install Microsoft Office and launch Word after a fresh installation;
- receive automatic Office updates and install updates with **Update Now**;
- create, edit, save, close, and reopen DOCX files;
- use core UI, tables, Shapes, WordArt, comments, Save As, PDF export, and print
  preview;
- sign in and retain Microsoft 365 Apps for enterprise activation after restarts;
- use RTL-language input, resources, UI, and text rendering.

Excel was also tested for core workbook editing, formulas, charts, comments,
CSV import, save/reopen, and print/PDF workflows. VBA has not yet been tested.

Word and Excel have not been exhaustively tested. These results do not cover
every feature, file type, add-in, macro, printer, or cloud service.

## Why this will not be submitted to WineHQ

WineHQ's
[Clean Room Guidelines](https://gitlab.winehq.org/wine/wine/-/wikis/Clean-Room-Guidelines)
state:

> Don't use an LLM tool to generate code. There's no guarantee that the training
> material of that LLM respects our Clean Room Guidelines, or that its output is
> compatible with the LGPL.

Wine4Office was produced with AI, so it cannot meet this rule. These changes will
**not** be pushed or submitted to the official WineHQ repository.

---

## Technical details

### Base

Wine4Office is based on **Wine 11.12**
([`996020f410e`](https://gitlab.winehq.org/wine/wine/-/commit/996020f410e7a1aa2dd6b44cf740854ea524d31a))
with Wine4Office compatibility changes through this branch. Relative to that base,
the branch changes 151 paths with about 15,938 insertions and 691 deletions.

### Main changes

- **Startup and licensing:** SPPC behavior, App-V dependencies, COM activation,
  EnterpriseData WinRT, MSXML, RetailInfo, Windows JSON, AppCapability, and
  RichEdit APIs used by Word.
- **Graphics:** DXGI/D3D11 shared resources, KMT handles, keyed mutexes, D2D SVG,
  shape shaders, WIC target reuse, geometry fixes, and GDI+ antialiasing.
- **Wayland and RTL languages:** popup stacking, caption controls, input regions,
  XKB language reporting, and preferred-UI-language handling.
- **Installation, updates, and sign-in:** Office Click-to-Run installation and
  update downloads through ranged BITS and an Office-compatible Delivery
  Optimization service, plus `CoCancelCall`, COM teardown, WinHTTP/WinINet
  compatibility, WAM/OneAuth objects, and federated MSHTML navigation.
- **OAuth:** `wine4officeauth.exe` runs inside the prefix, uses PKCE, handles federated
  returns, and stores WAM state with DPAPI under
  `%LOCALAPPDATA%\Wine4Office\WAM` without logging tokens.

Wine's metric-compatible Tahoma fonts were extended with the RTL glyph ranges
used by Office. Font generation and licensing details are in
[`fonts/README.wine4office-tahoma-hebrew.md`](fonts/README.wine4office-tahoma-hebrew.md)
and [`fonts/LICENSE.Liberation`](fonts/LICENSE.Liberation).

### Software Protection Platform (SPP/SPPC)

Wine4Office implements the `sppc.dll` subset Office uses for local Grace checks. It
selects the first valid installed SKU from Click-to-Run's `ProductReleaseIds`
and `Licenses16` XML/XRM metadata, with x64, x86, and multi-product support.

This passed 80/80 probes across 76 metadata profiles and four fallback or
multi-product cases. Word 2024 and Microsoft 365 ProPlus fallbacks remain;
volume activation is still stubbed, and SPPC does not grant a subscription.
See [`dlls/sppc/sppc.c`](dlls/sppc/sppc.c).

### Microsoft 365 subscription licensing

SPP handles local checks; WAM/OneAuth handles the subscription. `wine4officeauth.exe`
uses OAuth/PKCE and stores tokens with DPAPI. The tested organizational
subscription remained activated after restarts. KMS, MAK, VL, perpetual, retail,
device, shared-computer, and offline activation remain untested.

### Delivery Optimization

Wine4Office exposes Office's legacy Delivery Optimization COM interfaces through
`qmgr`, backed by Wine's BITS and WinHTTP transfer engine. It supports file and
`IStream` sinks, file/job properties, swarm statistics, and arbitrary CDN byte
ranges. COM proxy security blankets and generated 32/64-bit proxy/stub code let
Office use the service across process boundaries.

A clean test installation completed with exit code 0 after Office transferred
CABs and stream data through this path, including one request containing 2,576
ranges. Word then launched, its Account page showed automatic updates enabled,
and **Update Now** reported the installed build current. This implementation is
direct HTTP/CDN compatibility only; Windows peer discovery, peer sharing, and
full Delivery Optimization cache policy are not implemented.

### Build

Use an out-of-tree build after installing Wine's normal dependencies:

```sh
SOURCE="$PWD"
mkdir -p ../wine4office-build
cd ../wine4office-build
"$SOURCE/configure" --enable-archs=i386,x86_64
make -j"$(nproc)"
sudo make install
```

See WineHQ's [build guide](https://gitlab.winehq.org/wine/wine/-/wikis/Building-Wine)
for dependencies. Office, credentials, tokens, and proprietary Microsoft fonts
are not included.

### Limitations

Some implementations are minimal and Word-specific. Native behavior still needs
focused tests, diagnostics remain in the tree, and the code has not received an
independent C/C++ audit. Do not assume it is safe or correct for unrelated
Windows applications.

More details are in
[`OFFICE365-WORK-IN-PROGRESS.md`](OFFICE365-WORK-IN-PROGRESS.md).

### License

Wine4Office retains Wine's GNU LGPL 2.1-or-later license. See [`LICENSE`](LICENSE)
and [`COPYING.LIB`](COPYING.LIB). Liberation Sans glyph material uses the SIL
Open Font License.

Wine4Office is not affiliated with or endorsed by WineHQ or Microsoft.
