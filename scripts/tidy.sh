#!/usr/bin/env bash
# Run clang-tidy across engine/ and apps/ via compile_commands.json.
#
# One process per file, several files at a time. clang-tidy in LLVM 22
# still crashes on some translation units that import C++20 modules (see
# the 2026-09-07 entry in docs/decisions.md). A single batch invocation
# loses every finding when that happens, so each file is isolated: a
# crash costs that one file and is reported at the end instead of
# aborting the run.
#
# Exit status is non-zero when a file could not be checked. Findings
# themselves are reported but do not fail the run.
#
# TIDY_JOBS overrides how many files run at once (default: nproc).

set -euo pipefail

cd "$(dirname "$0")/.."

if [[ ! -f compile_commands.json ]]; then
    echo "compile_commands.json missing -- run 'make compdb' first" >&2
    exit 1
fi

mapfile -t files < <(
    find engine apps \
        -type f \( -name '*.cpp' -o -name '*.cppm' \) 2>/dev/null | sort
)

if [[ ${#files[@]} -eq 0 ]]; then
    echo "no source files yet -- nothing to tidy"
    exit 0
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# Output and exit status go to files named after the index so the parent
# can replay them in source order however the jobs interleave.
parallel=${TIDY_JOBS:-$(nproc)}
for i in "${!files[@]}"; do
    while (($(jobs -rp | wc -l) >= parallel)); do
        wait -n
    done
    # The subshell's own stderr is dropped: clang-tidy's output is already
    # captured, so all that is left is the shell announcing a killed child.
    (
        status=0
        clang-tidy --quiet -p . "${files[$i]}" >"$work/$i.out" 2>&1 || status=$?
        printf '%s\n' "$status" >"$work/$i.status"
    ) 2>/dev/null &
done
wait

warnings=0
crashed=()
failed=()
crash_head=20
for i in "${!files[@]}"; do
    status=$(<"$work/$i.status")
    warnings=$((warnings + $(grep -c ' warning: ' "$work/$i.out" || true)))
    if ((status >= 128)); then
        # A crash dumps a stack trace and the AST it died on, thousands
        # of lines. The top is the useful part; rerun clang-tidy on the
        # single file for the rest.
        crashed+=("${files[$i]} (signal $((status - 128)))")
        echo "tidy: clang-tidy crashed on ${files[$i]}:"
        head -n "$crash_head" "$work/$i.out"
        lines=$(wc -l <"$work/$i.out")
        if ((lines > crash_head)); then
            echo "  ... $((lines - crash_head)) more lines suppressed"
        fi
        continue
    fi
    cat "$work/$i.out"
    if ((status != 0)); then
        failed+=("${files[$i]} (exit $status)")
    fi
done

skipped=$((${#crashed[@]} + ${#failed[@]}))
echo "tidy: $((${#files[@]} - skipped))/${#files[@]} files checked, $warnings warning(s)"

if ((${#crashed[@]} > 0)); then
    echo "tidy: clang-tidy crashed on these files; they were NOT checked:" >&2
    printf '  %s\n' "${crashed[@]}" >&2
fi
if ((${#failed[@]} > 0)); then
    echo "tidy: clang-tidy reported errors on these files:" >&2
    printf '  %s\n' "${failed[@]}" >&2
fi

((skipped == 0))
