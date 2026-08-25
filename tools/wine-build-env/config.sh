#!/usr/bin/env bash

wine_build_repository_variable() {
    local name=$1
    local value=${!name:-}
    if [[ -z "$value" ]]; then
        command -v gh >/dev/null || {
            echo "$name must be set in the environment or as a GitHub repository variable" >&2
            return 1
        }
        local config_dir
        config_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
        value=$(
            cd "$config_dir"
            gh variable list --json name,value \
                --jq ".[] | select(.name == \"$name\") | .value"
        )
    fi
    [[ -n "$value" && "$value" != *$'\n'* ]] || {
        echo "Missing or invalid build configuration: $name" >&2
        return 1
    }
    printf '%s' "$value"
}

wine_build_remote_host() {
    local value
    value=$(wine_build_repository_variable WINE365_REMOTE_HOST)
    [[ "$value" =~ ^[A-Za-z0-9._-]+(@[A-Za-z0-9._-]+)?$ ]] || {
        echo "WINE365_REMOTE_HOST has an unsafe format" >&2
        return 1
    }
    printf '%s' "$value"
}

wine_build_remote_root() {
    local value
    value=$(wine_build_repository_variable WINE_BUILD_REMOTE_ROOT)
    [[ "$value" =~ ^/[A-Za-z0-9._/-]+/_wine-build$ && "$value" != *'/../'* ]] || {
        echo "WINE_BUILD_REMOTE_ROOT must be an absolute path ending in /_wine-build" >&2
        return 1
    }
    printf '%s' "$value"
}

wine_build_repo_url() {
    local value
    value=$(wine_build_repository_variable WINE_BUILD_REPO_URL)
    [[ "$value" != *[[:space:]]* ]] || {
        echo "WINE_BUILD_REPO_URL cannot contain whitespace" >&2
        return 1
    }
    printf '%s' "$value"
}
