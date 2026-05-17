#!/usr/bin/env bash
# Refresh the /compile_commands.json symlink to point at the active preset's
# build directory. clangd and clang-tidy both read this file from the repo
# root.

set -euo pipefail

cd "$(dirname "$0")/.."

preset="${1:-debug}"
src="build/${preset}/compile_commands.json"

if [[ ! -f "$src" ]]; then
    echo "${src} not found -- run 'make configure PRESET=${preset}' first" >&2
    exit 1
fi

ln -sf "$src" compile_commands.json
echo "compile_commands.json -> ${src}"
