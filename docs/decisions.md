# Decisions

A running log of design and tooling choices for Gorden/Roboslop. Newest
first. Each entry stays tight: what was decided, why, and where it lives.
If a choice is later changed, append a new entry that supersedes the old
one — don't edit in place.

---

## 2026-05-17 — Push policy: agents never push

**Decision.** No automation pushes to the remote. The user pushes manually
when they decide to publish work.

**Why.** Keeps publication a conscious step rather than a side effect of
landing a commit.

**Where.** Workflow constraint; not enforced in code.

---

## 2026-05-17 — bgfx, miniaudio via CMake FetchContent

**Decision.** Pull bgfx (+ bx, bimg, shaderc) and miniaudio at configure
time via CMake `FetchContent` with pinned SHAs/tags. Sources land under
`build/<preset>/_deps/` (gitignored), never in the working tree.
`imgui_impl_bgfx` is deferred until the dev-UI subsystem needs it.

**Why.** A custom Conan recipe was the original plan (~3–5 days of work);
submodules were rejected for repo overhead and recursive-init cost.
FetchContent keeps the working tree free of upstream sources. The recipe
path can be re-introduced later by mechanical conversion.

**Where.** [`third_party/CMakeLists.txt`](../third_party/CMakeLists.txt).
SHA pins live as cache variables at the top of that file.

---

## 2026-05-17 — `-fno-exceptions` is project-wide, tests included

**Decision.** Engine, game, and test binaries all compile with
`-fno-exceptions`. Catch2 uses `CATCH_CONFIG_DISABLE_EXCEPTIONS` so its
macros expand to a TRY/CATCH abstraction that compiles cleanly.

**Why.** C++23 module PCMs are sensitive to compiler config — a mismatch
on `-fno-exceptions` between producer and consumer rejects the PCM with a
"configuration mismatch" error. Keeping code-gen uniform avoids the trap.

**Where.** [`cmake/Modules.cmake`](../cmake/Modules.cmake)
`roboslop_apply_language_defaults()`. Test target reuses the same helper.

---

## 2026-05-17 — Error handling: per-subsystem enums + lightweight wrapper

**Decision.** Each engine subsystem defines its own `enum class FooError`
plus a `toError(FooError) -> roboslop::Error` mapping. The wrapper
`roboslop::Error { category, code, message, context }` is what crosses API
boundaries. `Result<T> = std::expected<T, Error>`.

**Why.** A single global enum forces `core` to know about every subsystem
and doesn't scale. `std::expected<T, std::string>` wastes an allocation on
every error and loses the ability to branch on category.

**Where.** [`roboslop/src/core/error.cppm`](../roboslop/src/core/error.cppm).
Subsystem-specific enums and mappings land with each subsystem.

---

## 2026-05-17 — C++23 modules-first; no internal headers; no `import std;`

**Decision.** Engine and game code lives in `.cppm` module interface units.
One module per logical unit; partitions only for multi-implementation
cases (e.g. LLM backends). No `include/` tree for internal code; headers
are tolerated only under `src/<subsystem>/shims/` for non-modularised
C-API bridges. `import std;` stays off — use classical `#include <print>`,
`#include <expected>` etc. inside `.cppm` files.

**Why.** Modules give better build times and cleaner interfaces.
`import std;` lacks mature shipping support in libc++/libstdc++ for the
supported compilers; revisit once that lands.

**Where.** [`cmake/Modules.cmake`](../cmake/Modules.cmake) defines
`roboslop_add_module_library()`. `roboslop/src/core/{version,error}.cppm`
serve as the reference shape.

---

## 2026-05-17 — Minimum compilers: Clang 19+ / GCC 15+

**Decision.** Promised support is Clang 19+ and GCC 15+. MSVC is tracking
but unvalidated.

**Why.** Realistic floor for C++23 modules through CMake
`FILE_SET cxx_modules`. Older versions have known module-scanner bugs and
partial standard-library support for `std::print`, `std::expected`,
`<ranges>`.

**Where.** [`README.md`](../README.md) prerequisites.

---

## 2026-05-17 — Build system: CMake + Conan + Ninja, Makefile entry point

**Decision.** CMake 3.30+ with Ninja as the only supported generator,
Conan 2.x for Conan-Center dependencies with one profile per
(OS, compiler), CMakePresets for tool integration, Makefile as the
human-facing entry point. Five presets: `debug`, `release`,
`relwithdebinfo`, `asan-ubsan` (per-target sanitiser flags), `tsan`
(dedicated Conan profile that rebuilds dependencies with
`-fsanitize=thread`).

**Why.** Ninja is the only generator with reliable C++23-module scanning.
Conan + CMakePresets gives reproducible toolchain wiring without manual
flag chains. The Makefile keeps the common loop a single keystroke long.

**Where.** [`CMakeLists.txt`](../CMakeLists.txt),
[`CMakePresets.json`](../CMakePresets.json),
[`conanfile.py`](../conanfile.py),
[`conan/profiles/`](../conan/profiles/),
[`Makefile`](../Makefile),
[`scripts/`](../scripts/).

---

## 2026-05-17 — Coding style: LLVM-base format, pragmatic tidy, Go-inspired naming

**Decision.** `.clang-format` based on LLVM with 4-space indent, Attach
braces, column 100, include-blocks regrouped (system → stdlib → roboslop
→ gorden → other). `.clang-tidy` enables the
bugprone / modernize / performance / readability / cppcoreguidelines /
misc / portability / concurrency families, with ~14 disables for game-dev
pragmatics (magic-numbers, pointer-arith, reinterpret-cast, POD member
visibility, etc.). Naming rules per
[`docs/conventions/code-style.md`](conventions/code-style.md).

**Why.** Modern C++ baseline, but practical for game and graphics code
where bit-twiddling and POD structs are routine; the disabled checks can
be re-enabled selectively in hot paths.

**Where.** [`.clang-format`](../.clang-format),
[`.clang-tidy`](../.clang-tidy).
