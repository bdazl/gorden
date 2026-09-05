# Roboslop

Roboslop is an experimental but serious C++23 platform for real-time
rendering, games and simulation, AI/LLM-integrated interactive systems,
and the tools and graphical experiments that grow up around them. The
core can be used as a general game engine, but it is not designed as an
abstract "universal engine" in a vacuum: engine features are pulled into
existence by concrete applications.

This repository is a monorepo:

- `engine/` — the Roboslop engine, a static library whose public
  surface is a set of C++23 modules under the `roboslop` namespace.
- `apps/gorden/` — **Gorden**, a future single-player top-down 3D game
  with an LLM-controlled robot companion. Today it doubles as the
  gameplay/AI sandbox and the debug/demo app where new engine features
  are first exercised.
- `apps/shaderlab/` — **Shader Lab**, a live shader sandbox: edit a
  shader on disk, it is recompiled through `shaderc` and swapped into
  the running scene; compile errors show in a panel while the last
  working program keeps rendering.

Further applications (a level editor) are planned; see the
[roadmap](docs/roadmap.md). Each is created when work on it starts, not
before.

## Status

The engine is past "hello world" but well short of a product. What
exists today, all driven by the Gorden demo:

- ECS facade over EnTT (`roboslop.ecs`).
- Semi-fixed timestep loop with a configurable fixed rate.
- Taskflow-backed system scheduling: systems declare the resources
  they read and write; the scheduler derives a DAG once and reuses it.
- Render graph over bgfx view-IDs, with a frontend/backend split: draw
  items are collected into a per-frame arena, sorted by a packed key,
  and submitted with no heap allocation in the render loop.
- Windowing and polled per-frame input via GLFW, with a free-fly debug
  camera.
- Jolt physics as three fixed-update systems (spawn, step, sync back to
  transforms).
- Asset loading: meshes via Assimp, textures via stb_image, a
  `Material` component, and an `AssetCache` that owns GPU-side
  programs, textures, and uniforms.
- One forward directional light with Lambert shading.
- Audio foundation via miniaudio: device, 3D listener and source
  systems.
- Animation data layer: skeleton, clips, CPU clip sampling, and an
  animation-state tick system. No GPU skinning yet.

- Dev UI: Dear ImGui over GLFW + bgfx (`roboslop.ui`), opt-in per app.
- Runtime shader compilation through the `shaderc` binary and hot
  replacement of a running program.

Not yet started: any LLM/agent code, serialisation, scene files. The Gorden executable is a physics/rendering
demo scene, not a game, which is intentional at this stage.

## Prerequisites

- **Clang 19+** or **GCC 15+** (C++23 modules through CMake
  `FILE_SET cxx_modules`). MSVC is tracking but not validated.
- **CMake 3.30+**, **Ninja**.
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
make run APP=shaderlab
make shaders       # compile registered shaders through bgfx shaderc
```

Other presets: `make build PRESET=release|relwithdebinfo|asan-ubsan|tsan`.
The `tsan` preset uses a dedicated Conan profile that rebuilds the entire
dependency graph with `-fsanitize=thread` — first run takes 10–20 minutes
and bifurcates the Conan cache.

`make help` lists every target.

## Documentation

- [Architecture](docs/architecture.md) — engine/app split, working
  principle, subsystem map, AI direction.
- [Roadmap](docs/roadmap.md) — milestones and open questions.
- [Build system](docs/build-system.md)
- [Decisions log](docs/decisions.md) — append-only history of choices.
- [Commit conventions](docs/conventions/commits.md)
- [Code-style conventions](docs/conventions/code-style.md)
- [Original repo-skeleton brief](docs/prompts/initial-repo-skeleton.md)
  (historical; predates the Roboslop repositioning)

## Licence

All rights reserved — licence to be decided. See [`LICENSE`](LICENSE).
