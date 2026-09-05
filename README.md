# Gorden

A single-player, top-down 3D game built on **Roboslop**, a custom C++23 game
engine designed for first-class LLM integration. The central mechanic is a
robot companion, played by a large language model, that perceives the world
through structured snapshots, acts via tool-calls, and can be customised by
the player through modular upgrades and personality tuning.

This repository is a monorepo:

- `engine/` — the Roboslop engine, in library form. Engine-as-library; game-agnostic.
- `apps/gorden/` — the Gorden game built on top of it.

## Status

Hello-world runs. The build system, source skeleton, and tooling are in
place; engine subsystems land in follow-up passes. The running log of
design and tooling choices lives in
[`docs/decisions.md`](docs/decisions.md).

## Prerequisites

- **Clang 19+** or **GCC 15+** (C++23 modules through CMake
  `FILE_SET cxx_modules`). MSVC is tracking but not validated.
- **CMake 3.30+** (3.31 recommended), **Ninja**.
- **Conan 2.x**, **Python 3.10+**.
- **Linux X11/Wayland dev libs**: glfw pulls `xorg/system` which probes
  pkg-config for `libxres`, `libxcb`, `libxcursor`, `libxinerama`,
  `libxrandr`, `libxi`, `libxkbcommon`. On Arch:
  `sudo pacman -S libxres libxcb libxcursor libxinerama libxrandr libxi libxkbcommon`.
  Conan profiles ship with `tools.system.package_manager:mode=report` so
  Conan never runs `pacman`/`apt` itself — install the libs manually.

## Quickstart

```sh
make bootstrap     # conan install for the default preset (debug)
make configure     # cmake --preset debug
make build         # cmake --build --preset debug
make test          # ctest --preset debug
make run           # ./build/debug/apps/gorden/gorden
make shaders       # compile sample shaders through bgfx shaderc
```

Other presets: `make build PRESET=release|relwithdebinfo|asan-ubsan|tsan`.
The `tsan` preset uses a dedicated Conan profile that rebuilds the entire
dependency graph with `-fsanitize=thread` — first run takes 10–20 minutes
and bifurcates the Conan cache.

`make help` lists every target.

## Documentation

- [Architecture overview](docs/architecture.md)
- [Build system](docs/build-system.md)
- [Decisions log](docs/decisions.md)
- [Commit conventions](docs/conventions/commits.md)
- [Code-style conventions](docs/conventions/code-style.md)
- [Original brief](docs/prompts/initial-repo-skeleton.md)

## Licence

All rights reserved — licence to be decided. See [`LICENSE`](LICENSE).
