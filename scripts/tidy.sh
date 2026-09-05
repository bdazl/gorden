#!/usr/bin/env bash
# Run clang-tidy across engine/ and apps/ via compile_commands.json.

set -euo pipefail

cd "$(dirname "$0")/.."

if [[ ! -f compile_commands.json ]]; then
    echo "compile_commands.json missing -- run 'make compdb' first" >&2
    exit 1
fi

mapfile -t files < <(
    find engine apps \
        -type f \( -name '*.cpp' -o -name '*.cppm' \) 2>/dev/null
)

if [[ ${#files[@]} -eq 0 ]]; then
    echo "no source files yet -- nothing to tidy"
    exit 0
fi

clang-tidy --quiet -p . "${files[@]}"
