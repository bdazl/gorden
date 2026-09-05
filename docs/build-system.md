# Build system

How the toolchain is laid out and how to drive it. The companion log of
trade-offs behind these choices is in [`docs/decisions.md`](decisions.md).

## Toolchain

- **CMake 3.30+** with **Ninja** as the only supported generator (required
  for reliable C++23-module scanning).
- **Conan 2.x** for Conan-Center dependencies; profiles live under
  [`conan/profiles/`](../conan/profiles/).
- **CMakePresets** at the repo root drives configure/build/test for each
  flavour.
- **Makefile** is the human entry point — every common task is one keystroke.

## Presets

| Preset | Config | Sanitisers | Notes |
|---|---|---|---|
| `debug`          | Debug          | none       | Default. |
| `release`        | Release        | none       | |
| `relwithdebinfo` | RelWithDebInfo | none       | |
| `asan-ubsan`     | Debug          | asan+ubsan | Per-target only; deps stay clean. |
| `tsan`           | Debug          | tsan       | Rebuilds the dep graph; see below. |

Each preset writes to `build/<preset>/`. Switching presets does **not**
invalidate other build directories.

## Workflow

```sh
make bootstrap PRESET=debug     # conan install, writes the toolchain file
make configure                  # cmake --preset
make build                      # cmake --build --preset
make test                       # ctest --preset
make run [ARGS=...]             # ./build/<preset>/apps/gorden/gorden
make shaders                    # compile every registered shader via shaderc
```

`make build` does **not** auto-bootstrap. If the Conan toolchain is missing,
it fails with cmake's preset error pointing at `make bootstrap`. `make help`
lists every target.

## Conan profiles

One profile per `(OS, compiler)`, plus a dedicated `linux-clang-tsan`:

```
conan/profiles/
├── linux-clang-debug
├── linux-clang-release
├── linux-clang-tsan
├── linux-gcc-debug
├── linux-gcc-release
├── macos-clang-release
└── windows-msvc-release
```

`scripts/bootstrap.sh` detects OS + compiler, maps the preset to a profile,
and runs `conan install` with both `--profile:host` and `--profile:build`
set so Conan's auto-detected default profile can never leak in.

`tools.system.package_manager:mode=report` is set on every Linux profile so
Conan never runs `pacman`/`apt` itself. System dev libs (libxres, libxcb,
libxkbcommon, ...) are the user's responsibility — see
[`README.md`](../README.md).

## Sanitisers

**asan-ubsan.** Applied to roboslop targets only via the
`ROBOSLOP_SANITIZERS` cache variable (set by the preset). The dependency
graph is built clean; sanitiser instrumentation crosses ABI boundaries
without issue.

**tsan.** A separate `linux-clang-tsan` profile rebuilds the entire dep
graph with `-fsanitize=thread` because bgfx, Jolt, and GLFW all spawn their
own threads — they must be instrumented too. First bootstrap under this
preset takes 10–20 minutes and bifurcates the Conan cache.

## Adding a dependency

| Where | How |
|---|---|
| On Conan Center      | `self.requires("name/version")` in [`conanfile.py`](../conanfile.py); `find_package(Name CONFIG REQUIRED)` from CMake. |
| Not on Conan Center  | `FetchContent_Declare` in [`third_party/CMakeLists.txt`](../third_party/CMakeLists.txt) with a pinned SHA or tag. Fetched sources land under `build/<preset>/_deps/` (gitignored). |
| Custom Conan recipe  | Reserved for cases where FetchContent doesn't suffice; nothing in the tree today. |

bgfx (+ bx, bimg, shaderc) and miniaudio currently come in via FetchContent.
The `imgui_impl_bgfx` slot is intentionally empty until the dev-UI subsystem
lands a minimal local implementation.

## C++23 module notes

- `CXX_SCAN_FOR_MODULES` is on globally; engine and app targets opt into
  modules through `roboslop_add_module_library()` which wires
  `FILE_SET cxx_modules` together with the language defaults bundle.
- `import std;` stays off. Use the global module fragment +
  `#include <print>` (etc.) inside a `.cppm`.
- **PCMs are sensitive to compiler config.** Every target that imports
  roboslop modules — engine, apps, tests — must apply
  `roboslop_apply_language_defaults()` so producer and consumer agree on
  language-level flags; otherwise a PCM is refused for "configuration
  mismatch". Exceptions are left at the compiler default (enabled); see
  [`docs/conventions/code-style.md`](conventions/code-style.md) for the
  error-handling convention that replaces the old `-fno-exceptions` rule.
- bgfx.cmake force-downgrades `CMAKE_CXX_STANDARD` to 20 globally when
  added; the top-level `CMakeLists.txt` restores 23 after
  `add_subdirectory(third_party)` so later subdirectories see the C++23
  baseline.
