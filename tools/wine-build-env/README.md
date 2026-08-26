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

## Remote configuration

Remote commands read `WINE365_REMOTE_HOST`, `WINE_BUILD_REMOTE_ROOT`, and
`WINE_BUILD_REPO_URL` from the environment. When a value is absent, they load
the matching GitHub repository variable with `gh`. Configure all three
repository variables before creating an agent build; successful configuration
leaves no server identity or user path in the source tree.
