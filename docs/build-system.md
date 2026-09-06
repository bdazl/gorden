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
make build                      # cmake --build --preset (engine, every app, tests)
make test                       # ctest --preset
make shaders                    # compile every registered shader via shaderc
```

### Per-app development loop

Apps are discovered from `apps/*/`; each builds an executable named after
its directory. `make apps` lists them.

```sh
make shaderlab                  # build only shaderlab (+ engine, its shaders), then run it
make gorden ARGS="..."          # same for gorden; ARGS are forwarded
make build-<app>                # build only <app>
make run-<app>                  # run without building
make run APP=<app>              # alias for run-<app>; APP defaults to gorden
```

`make <app>` is the inner loop while working on one application: it
rebuilds only that app's target and what it depends on, then launches it
from `build/<preset>/` so the relative asset root resolves. The `debug`
preset is the development flavour (asserts on, bgfx debug output, no
optimisation); pass `PRESET=release` for performance runs or
`PRESET=asan-ubsan` / `tsan` for sanitised runs. For Shader Lab the
edit-while-running hot reload means one launch usually lasts a whole
session.

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
glfw is built with both Linux backends (`glfw/*:with_wayland=True`, set
from `configure()` in `conanfile.py`); the recipe then builds libwayland,
xkbcommon and wayland-protocols from source, and GLFW chooses the platform
at runtime. Those Conan-built libraries are build-time only: GLFW
`dlopen()`s libwayland-client, libwayland-cursor, libwayland-egl and
libxkbcommon by soname, so `engine/CMakeLists.txt` strips them from the
glfw link interface and keeps just the Conan wayland header (needed by
`glfw3native.h`). At runtime the session's own copies are used; a Wayland
desktop always ships them.
libcurl comes from Conan with its default options, which on Linux means an
OpenSSL-backed HTTPS stack; the first `make bootstrap` after adding it
builds OpenSSL from source (several minutes, once per Conan cache).
Dear ImGui comes from Conan; its GLFW platform backend is compiled directly
from the package's `res/bindings/` directory (located through the
`imgui_PACKAGE_FOLDER_<CONFIG>` variable CMakeDeps generates), and the
bgfx renderer backend is our own TU in `engine/src/ui/`. Nothing ImGui-
related is fetched or vendored.

## Options

| Option | Default | Effect |
|---|---|---|
| `ROBOSLOP_DEV_UI` | `ON` | `App` honours `AppConfig::enableDevUi` and the `shaderlab` app is configured. `OFF` still builds `roboslop.ui` (the module is unconditional to keep `App` free of `#if`-guarded members) but `App` logs a warning and skips the dev UI. |
| `ROBOSLOP_BUILD_TESTS` | `ON` | Build the Catch2 test executable. |
| `ROBOSLOP_SANITIZERS` | empty | See "Sanitisers". |

## Per-user files

`roboslop.core.paths` resolves the XDG base directories, always with a
`roboslop/` subdirectory: config under `$XDG_CONFIG_HOME/roboslop`
(default `~/.config/roboslop`), data under `$XDG_DATA_HOME/roboslop`
(default `~/.local/share/roboslop`). Apps keep their settings JSON and
ImGui layout ini in the former and persistent user data in the latter.

## Runtime environment variables

Read by the applications, never by the build:

| Variable | Used by | Effect |
|---|---|---|
| `OPENAI_API_KEY` | gorden | Selects the OpenAI-compatible LLM backend. Absent → scripted demo provider. Never logged. |
| `OPENAI_BASE_URL` | gorden | Base URL of the chat-completions API (default `https://api.openai.com/v1`; e.g. `http://127.0.0.1:8080/v1` for a llama.cpp server). |
| `GORDEN_MODEL` | gorden | Model name sent in the request (default `gpt-4.1-mini`). |

## Shaders

`cmake/ShaderCompile.cmake` wraps bgfx's `bgfx_compile_shaders()`. Every
`roboslop_compile_shader()` call writes `<build>/assets/shaders/<backend>/<name>.sc.bin`
and registers a per-call custom target under the aggregate `shaders`
target (`make shaders`). The aggregate is *not* part of `all`; a target
that loads shaders at runtime should pass `TARGET_VAR` and
`add_dependencies()` on the result so `make build` alone produces a
runnable binary (the engine does this for its ImGui pair).

## C++23 module notes

- `CXX_SCAN_FOR_MODULES` is on globally; engine and app targets opt into
  modules through `roboslop_add_module_library()` which wires
  `FILE_SET cxx_modules` together with the language defaults bundle.
- `import std;` stays off. Use the global module fragment +
  `#include <print>` (etc.) inside a `.cppm`.
- Modules are the default, not a requirement. A plain `.cpp`/`.c` TU or a
  header is added via `SOURCES` in `roboslop_add_module_library()` when
  that is the simpler integration (see the two third-party implementation
  TUs in `engine/CMakeLists.txt`).
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
