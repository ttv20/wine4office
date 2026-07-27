# Wine4Office Manager

Wine4Office Manager is included in the Wine4Office source tree at `tools/wine4office-manager`. It manages a prebuilt Wine4Office runner, prefixes, Office shortcuts, updates, and removal.

The interface is a native Qt Widgets desktop application. It does not start an HTTP server, embed a web view, or open a browser. Release bundles include a standalone Qt manager binary, so end users do not need to install Python packages.

## Install

End users install the prebuilt Wine runner and manager together by downloading the single executable `.run` artifact from a GitHub Release and running it without root.

For manager development only, install PySide6 and then install the manager without a runner from the Wine source root:

```bash
python3 -m pip install --user -r tools/wine4office-manager/requirements-gui.txt
./tools/wine4office-manager/install-user.sh
```

A standalone development binary can be built in a virtual environment:

```bash
python3 -m venv .venv-manager
.venv-manager/bin/pip install -r tools/wine4office-manager/requirements-build.txt
PYTHON=$PWD/.venv-manager/bin/python \
  tools/wine4office-manager/packaging/build-qt-binary.sh ./wine4office-manager-qt
QT_QPA_PLATFORM=offscreen ./wine4office-manager-qt --smoke-test
```

Open **Wine4Office Manager** from the application menu, or run:

```bash
~/.local/bin/wine4office-manager
```

The manager defaults to:

- Wine prefix: `~/.wine4office`
- User runner: `${XDG_DATA_HOME:-~/.local/share}/wine4office/runner`
- Update manifest: not configured

## Features

- Create or rollback-safely recreate a selected Wine prefix.
- Run an Office installer or any selected `.exe` inside that prefix through a native Qt file picker, with optional arguments.
- Create/update/remove Word, Excel, PowerPoint, and Outlook shortcuts.
- Optionally copy shortcuts to the visible Desktop.
- Open Wine Configuration, Registry Editor, Control Panel, File Manager, Command Prompt, Task Manager, and Uninstaller.
- Use the Wine runner prebuilt by GitHub Actions and delivered inside the release `.run` file.
- Stream and cancel verified update downloads.
- Download a future update manifest over HTTPS, verify the installer SHA-256, and run the verified installer.
- Remove the manager, runner, configuration, and shortcuts; prefix deletion requires a separate explicit checkbox.

## One-file Wine + manager installer

`packaging/build-onefile.sh` combines an already compiled Wine runner and this manager into one executable `.run` file. Release builds pass the standalone Qt binary as the optional sixth argument:

```bash
tools/wine4office-manager/packaging/build-onefile.sh \
  /path/to/compiled/runner \
  release/wine4office-1.0.0-x86_64.run \
  1.0.0 "" "" ./wine4office-manager-qt
```

The resulting executable supports:

```bash
./wine4office-1.0.0-x86_64.run             # install for the current user
./wine4office-1.0.0-x86_64.run --update    # atomically replace the runner and refresh the manager
./wine4office-1.0.0-x86_64.run --version
./wine4office-1.0.0-x86_64.run --extract DIRECTORY
./wine4office-1.0.0-x86_64.run --uninstall # preserve ~/.wine4office
```

The embedded payload uses Zstandard level 19; `zstd` is required to build or run the bundle. Installation refuses root and writes below `${XDG_DATA_HOME:-~/.local/share}/wine4office`. Runner replacement keeps the previous runner until the new manager installation succeeds.

## Update manifest

No update address is configured yet. The format is ready for a future HTTPS endpoint:

```json
{
  "version": "1.1.0",
  "installer_url": "https://example.invalid/wine4office-1.1.0-x86_64.run",
  "sha256": "64 lowercase hexadecimal characters",
  "size": 123456789
}
```

The manifest address can later be embedded in a bundle, set in the manager, or supplied through the GitHub repository variable `WINE4OFFICE_UPDATE_MANIFEST_URL`. The installer URL can be supplied through `WINE4OFFICE_INSTALLER_URL`.

## GitHub CI/CD

`.github/workflows/wine4office-onefile.yml`:

1. Builds the merged Wine4Office tree on Ubuntu.
2. Installs it into a staging directory.
3. Builds the PySide6 interface as a standalone native Qt executable.
4. Creates one executable installer containing Wine and the Qt manager.
5. Uploads the release files and checksums as a CI artifact.
6. Publishes those files to a GitHub Release for tags named `wine4office-v*`.

The workflow can also be run manually with an explicit version and future update URLs. Builds target a `self-hosted`, `linux`, `x64` runner and wait if no matching runner is online. The workflow does not use Docker or require access to a Docker socket: it runs `configure` and `make` directly. A root runner container can install apt dependencies itself; an unprivileged container must include the listed build dependencies in its image.

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
tools/wine4office-manager/tests/test_onefile.sh
QT_QPA_PLATFORM=offscreen python3 tools/wine4office-manager/wine4office_manager.py --smoke-test
```
