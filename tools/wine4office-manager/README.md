# Wine4Office Manager

![Wine4Office banner](banner.png)

Wine4Office Manager is included in the Wine4Office source tree at `tools/wine4office-manager`. It manages a prebuilt Wine4Office runner, prefixes, Office shortcuts, verified updates, and removal.

The interface is a native Qt Widgets desktop application. It does not start an HTTP server, embed a web view, or open a browser. Releases provide a standalone Wine4Office Manager binary, so end users do not need to install Python packages.

## Install

End users download `Wine4OfficeManager-<version>-x86_64` and its `.sha256` file from the [GitHub release](https://github.com/ttv20/wine4office/releases). Verify the checksum, make the binary executable, and run it without root:

```bash
sha256sum -c Wine4OfficeManager-1.0.0-x86_64.sha256
chmod +x Wine4OfficeManager-1.0.0-x86_64
./Wine4OfficeManager-1.0.0-x86_64
```

Wine4Office Manager checks the configured `release.json` asynchronously whenever
the Manager opens. It does not download Wine or a manager update until the user
explicitly approves it.

On first launch, the Manager separately asks whether to enable background checks
at user login and every 24 hours. This opt-in installs a per-user systemd timer;
declining or disabling it does not turn off the check when the Manager opens.
Background update notifications include **Update**, which opens the Manager
directly on **Maintenance**, and **Disable automatic checks** actions.

For development only, install PySide6 and then install the manager from the Wine source root:

```bash
python3 -m pip install --user -r tools/wine4office-manager/requirements-gui.txt
./tools/wine4office-manager/install-user.sh
```

The installer asks whether to launch **Wine4Office Manager** when installation
finishes. Press Enter to accept the default, **Yes**. Set
`WINE4OFFICE_LAUNCH_MANAGER=yes` or `no` for non-interactive automation.

A standalone development binary can be built in a virtual environment:

```bash
python3 -m venv .venv-manager
.venv-manager/bin/pip install -r tools/wine4office-manager/requirements-build.txt
PYTHON=$PWD/.venv-manager/bin/python \
  tools/wine4office-manager/packaging/build-qt-binary.sh ./Wine4OfficeManager development stable
QT_QPA_PLATFORM=offscreen ./Wine4OfficeManager --smoke-test
```

Open **Wine4Office Manager** from the application menu, or run its installed compatibility launcher:

```bash
~/.local/bin/wine4office-manager
```

The manager defaults to:

- Wine prefix: `~/.wine4office`
- User runner: `${XDG_DATA_HOME:-~/.local/share}/wine4office/runner`
- Update metadata: the canonical URL saved during installation or the last validated update check

## Features

- Create or rollback-safely recreate a selected Wine prefix.
- Run an Office installer or any selected `.exe` inside that prefix through a native Qt file picker, with optional arguments.
- Install supported Office products through the current Office Deployment Tool,
  or separately download and run Microsoft's standalone Teams bootstrapper.
- Create/update/remove Word, Excel, PowerPoint, Outlook, and Office Language Preferences shortcuts.
- Apply curated per-prefix Office compatibility and privacy policies without
  exposing Windows-only Group Policy infrastructure.
- Optionally start Office Click-to-Run and App-V background services at login
  to make Office apps open 2–3× faster. They typically use 300–600 MB of RAM.
- Open Wine configuration and maintenance utilities.
- Download and atomically install separately verified Wine4Office Manager and
  Wine runner updates. Runner updates refresh the prefix with `wineboot -u`,
  then restore any background services that were running.
- Optionally check for updates at login and every 24 hours, with one-click
  disablement from the desktop notification.
- Remove the manager, runner, configuration, and shortcuts; prefix deletion requires separate explicit confirmation.

Office desktop files do not restart the standalone manager. Creating or updating
a shortcut atomically writes a manager-owned launcher under
`${XDG_DATA_HOME:-~/.local/share}/wine4office/shortcut-launchers/`. The launcher
applies the selected display mode and Office-specific setup before replacing
itself with Wine. Removing the shortcut also removes its generated launcher.

The **Office settings** page keeps policy controls out of the already dense
environment page. Compatibility controls can disable animations or hardware
acceleration and suppress First Run or the Office Start screen. Hardware
acceleration remains enabled by default. Privacy currently exposes the
ownership-safe Office telemetry policy while leaving connected experiences
enabled. Security explains why Internet macro blocking is not offered until
Linux download provenance can be bridged to Wine's `Zone.Identifier` handling.
All implemented settings are stored per prefix, and removal deletes a registry
value only when it still matches the value written by Wine4Office.

After a manager update, the newly installed binary runs versioned post-install
hooks. The current hook migrates only existing Wine4Office-owned Office
shortcuts; shortcuts the user removed are not recreated. The manager records
successful hook completion and retries pending hooks at the next startup. Once
the update task finishes, the UI offers to restart into the new manager.

## Separate release artifacts

`packaging/build-release-artifacts.sh` packages an already staged Wine runner and standalone Wine4Office Manager without combining or modifying them:

```bash
tools/wine4office-manager/packaging/build-release-artifacts.sh \
  /path/to/staged/runner \
  ./Wine4OfficeManager \
  ./release \
  1.0.0 \
  https://github.com/ttv20/wine4office/releases/latest/download/release.json \
  https://github.com/ttv20/wine4office/releases/latest/download \
  stable
```

The release directory contains:

- `Wine4OfficeManager-1.0.0-x86_64`
- `Wine4OfficeManager-1.0.0-x86_64.sha256`
- `wine4office-1.0.0-x86_64.tar.zst`
- `wine4office-1.0.0-x86_64.tar.zst.sha256`
- `release.json`

The workflow also publishes the repository's verified `install.sh` beside these generated artifacts.

The generic Zstandard Wine archive has one root, `wine4office-1.0.0-x86_64/`. Packaging reads the staged runner without changing it, does not add a `bin/wine64` compatibility link, and does not use consumer-specific archive naming.

## Provider-neutral release metadata

`release.json` uses schema version 1:

```json
{
  "schema_version": 1,
  "channel": "stable",
  "metadata_url": "https://github.com/ttv20/wine4office/releases/latest/download/release.json",
  "manager": {
    "version": "1.0.0",
    "url": "https://github.com/ttv20/wine4office/releases/latest/download/Wine4OfficeManager-1.0.0-x86_64",
    "sha256": "64 lowercase hexadecimal characters",
    "size": 12345678
  },
  "wine": {
    "version": "1.0.0",
    "url": "https://github.com/ttv20/wine4office/releases/latest/download/wine4office-1.0.0-x86_64.tar.zst",
    "sha256": "64 lowercase hexadecimal characters",
    "size": 123456789,
    "format": "tar.zst"
  }
}
```

Artifact URLs may be absolute HTTPS URLs or paths relative to `metadata_url`. A successfully validated `metadata_url` becomes the canonical address saved for future checks. This allows metadata and artifacts to move to the project's own server without a code change.

## GitHub CI/CD

`.github/workflows/wine4office-release.yml` runs manually and for `wine4office-v*` tags on a `self-hosted`, `linux`, `x64` GitHub Actions runner. It reuses the Wine configure/build/install staging process, builds Wine4Office Manager as a separate PyInstaller binary, packages the manager and Wine artifacts, and publishes `install.sh`. The manager pair, Wine pair, and release metadata plus installer are uploaded as separate CI artifacts.

Tagged runs use the workflow's `contents: write` permission and `GITHUB_TOKEN` to create the GitHub Release when needed, then upload the manager pair, Wine pair, `install.sh`, and finally `release.json` with `gh release upload --clobber`. Publishing `release.json` last keeps it as the feed commit marker. Reruns replace matching assets instead of creating duplicates.

Manual `metadata_url`, `release_base_url`, and `channel` inputs override repository variables `WINE4OFFICE_METADATA_URL`, `WINE4OFFICE_RELEASE_BASE_URL`, and `WINE4OFFICE_RELEASE_CHANNEL`. Metadata defaults to `https://github.com/ttv20/wine4office/releases/latest/download/release.json`, and artifacts default to the adjacent GitHub latest-release download directory. The standalone manager embeds the selected channel and rejects metadata from another channel. Set the URL variables to canonical project-hosted HTTPS addresses later without changing the workflow or application.

## Removal

Remove only the manager while preserving the runner, configuration, and prefix:

```bash
./tools/wine4office-manager/uninstall-user.sh
```

Remove manager, runner, and configuration while preserving the prefix:

```bash
~/.local/share/wine4office/bin/wine4office-uninstall --purge-runner
```

The manager UI provides both removal modes and requires explicit confirmation before deleting the selected prefix.

## Tests

```bash
python3 -m unittest discover -s tools/wine4office-manager/tests -v
tools/wine4office-manager/tests/test_release_artifacts.sh
tools/wine4office-manager/tests/test_curl_install.sh
QT_QPA_PLATFORM=offscreen python3 tools/wine4office-manager/wine4office_manager.py --smoke-test
```
