#!/usr/bin/env bash
# Run conan install for the given preset. Detects OS + compiler, selects the
# matching profile under conan/profiles/, and writes the toolchain file at
# build/<preset>/conan_toolchain.cmake.
#
# Usage: scripts/bootstrap.sh [preset]
# Default preset: debug.

set -euo pipefail

cd "$(dirname "$0")/.."

preset="${1:-debug}"

case "$(uname -s)" in
    Linux*)  os=linux ;;
    Darwin*) os=macos ;;
    MINGW*|MSYS*|CYGWIN*) os=windows ;;
    *) echo "unsupported OS: $(uname -s)" >&2; exit 1 ;;
esac

if [[ "$os" == "macos" ]]; then
    compiler=clang
elif [[ "$os" == "windows" ]]; then
    compiler=msvc
else
    case "${CXX:-${CC:-clang}}" in
        *clang*) compiler=clang ;;
        *g++*|*gcc*) compiler=gcc ;;
        *) compiler=clang ;;
    esac
fi

case "$preset" in
    debug)          profile="${os}-${compiler}-debug";   build_type=Debug ;;
    release)        profile="${os}-${compiler}-release"; build_type=Release ;;
    relwithdebinfo) profile="${os}-${compiler}-release"; build_type=RelWithDebInfo ;;
    asan-ubsan)     profile="${os}-${compiler}-debug";   build_type=Debug ;;
    tsan)           profile="${os}-clang-tsan";          build_type=Debug
                    echo "TSan build: first run rebuilds dependencies with -fsanitize=thread (10-20 min)." >&2 ;;
    *) echo "unknown preset: $preset" >&2; exit 1 ;;
esac

profile_path="conan/profiles/${profile}"
if [[ ! -f "$profile_path" ]]; then
    echo "profile not found: $profile_path" >&2
    exit 1
fi

echo "bootstrap: preset=${preset} profile=${profile_path} build_type=${build_type}"

conan install . \
    --output-folder="build/${preset}" \
    --profile:host="$profile_path" \
    --profile:build="$profile_path" \
    -s:h "build_type=${build_type}" \
    --build=missing
