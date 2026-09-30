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
tools/wine-build-env/run-build-container.sh runtime "$PWD" "$PWD/build" "$PWD/stage"
```

`targets` requires explicit make targets and refuses a dry run with more than
80 compile or link commands. For remote work, start with
`create-agent-build.sh` (which calls `refresh-main-build.sh`), then use
`sync-agent-source.sh`, `build-agent-targets.sh`, `install-agent-runner.sh`,
and `remove-agent-build.sh` in lifecycle order.

The release workflow and `install-agent-runner.sh` also run `runtime` against
the installed runner. It compiles and executes separate 32-bit and 64-bit
programs in new WoW64 prefixes, checking package and CoreApplication activation, ProtectionPolicy,
WAM factories, Compositor activation, and the DirectComposition Device3
interface. Missing DLLs or the authentication helper fail before execution.
This is a package check; it does not verify sign-in or GPU rendering. Full logs
and isolated prefixes remain in the reported build result directory.

For a comparison with the additional classes checked by Bottles/Soda, run:

```sh
tools/wine-build-env/check-office-runtime.sh /path/to/runner /new/result/directory --soda
```

The `--soda` readiness profile requires CoreApplication and MessageWebSocket
as well as ProtectionPolicyManager. CoreApplication and ProtectionPolicyManager
pass; MessageWebSocket is not implemented in Wine4Office. It is kept separate
from the release gate so this missing support is visible without claiming it works.

`probes/dxvk-composition.c` checks hardware D3D11 creation, offscreen pixel
readback, and `CreateSwapChainForComposition`. It never requests WARP. Compile
it for each x86 architecture using MinGW with `-ld3d11 -ldxgi -lole32 -luuid`.
Run it only on the user-selected GPU test laptop, using an isolated directory
with both DXVK `d3d11.dll` and `dxgi.dll`, plus a control run using native DLLs.
On Windows, use an interactive Scheduled Task: SSH's noninteractive session
can reject native composition creation independently of DXVK. A successful
creation does not yet verify presentation, clipping, focus, or Wine integration.

## Remote configuration

Remote commands read `WINE365_REMOTE_HOST`, `WINE_BUILD_REMOTE_ROOT`, and
`WINE_BUILD_REPO_URL` from the environment. When a value is absent, they load
the matching GitHub repository variable with `gh`. Configure all three
repository variables before creating an agent build; successful configuration
leaves no server identity or user path in the source tree.
