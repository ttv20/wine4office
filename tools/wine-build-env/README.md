# Wine4Office build environment

This directory is the single build contract for release and agent Wine builds.
The GitHub release workflow and the remote incremental lifecycle both build the
same Dockerfile and invoke `run-build-container.sh`.
The container defaults to 18 build jobs and an 18-CPU quota.

The build fails during configure when required Kerberos/GSSAPI development
support is unavailable. It also checks the configured Office capabilities and
the installed runner directly in `build.sh`; there is no separate capability
policy file.

## Direct use

```sh
tools/wine-build-env/run-build-container.sh full "$PWD" "$PWD/build" "$PWD/stage"
tools/wine-build-env/run-build-container.sh targets "$PWD" "$PWD/build" "$PWD/stage" \
  dlls/kerberos/kerberos.so
tools/wine-build-env/run-build-container.sh install "$PWD" "$PWD/build" "$PWD/stage"
```

`targets` requires explicit make targets and refuses a dry run with more than
80 compile or link commands. For remote work, start with
`create-agent-build.sh` (which calls `refresh-main-build.sh`), then use
`sync-agent-source.sh`, `build-agent-targets.sh`, `install-agent-runner.sh`,
and `remove-agent-build.sh` in lifecycle order.

## Bundled DXVK

`bundle-dxvk.sh STAGE_RUNNER_ROOT` downloads the pinned DXVK 3.1.1 release,
verifies its SHA-256, and installs only `dxgi`, `d3d11` and `d3d10core` (x64 and
x32) plus `manifest.json` and the vendored zlib license (`dxvk/LICENSE`) into
`STAGE_RUNNER_ROOT/share/wine4office/dxvk/3.1.1/`. The release workflow runs it
after the Gecko and Mono step, and release packaging refuses a runner without
the complete bundle. Agent runners skip it unless `WINE4OFFICE_BUNDLE_DXVK=1`
is set for `install-agent-runner.sh`; the server then needs HTTPS access to
GitHub and caches the verified tarball under `cache/dxvk` in the build root.
Without the bundle, Wine4Office Manager keeps the prefix on WineD3D.

## Remote configuration

Remote commands read `WINE365_REMOTE_HOST`, `WINE_BUILD_REMOTE_ROOT`, and
`WINE_BUILD_REPO_URL` from the environment. When a value is absent, they load
the matching GitHub repository variable with `gh`. Configure all three
repository variables before creating an agent build; successful configuration
leaves no server identity or user path in the source tree.
