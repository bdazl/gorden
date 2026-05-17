#!/usr/bin/env bash
# Run clang-format across roboslop/ and gorden/.
# `scripts/format.sh`         applies fixes in place.
# `scripts/format.sh --check` dry-run; fails on diffs (CI mode).

set -euo pipefail

cd "$(dirname "$0")/.."

mode=fix
if [[ "${1:-}" == "--check" ]]; then
    mode=check
fi

mapfile -t files < <(
    find roboslop gorden \
        -type f \( -name '*.cppm' -o -name '*.cpp' \
                -o -name '*.h'    -o -name '*.hpp' \) 2>/dev/null
)

if [[ ${#files[@]} -eq 0 ]]; then
    echo "no source files yet -- nothing to format"
    exit 0
fi

if [[ "$mode" == "check" ]]; then
    clang-format --dry-run --Werror --style=file "${files[@]}"
else
    clang-format -i --style=file "${files[@]}"
fi
