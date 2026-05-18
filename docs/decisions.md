# Decisions

A running log of design and tooling choices for Gorden/Roboslop. Newest
first. Each entry stays tight: what was decided, why, and where it lives.
If a choice is later changed, append a new entry that supersedes the old
one — don't edit in place.

---

## 2026-05-18 — Input is a polled per-frame module with a pure-helper split

**Decision.** `roboslop::Input` (module `roboslop.platform.input`) wraps GLFW
key/mouse polling. Construction captures the raw `GLFWwindow*` from
`Window::handle()` and never reseats — the underlying GLFW object has a
stable address, so Input survives any Window/App move without rebinding.
`Input::beginFrame()` is called once per render frame after
`pollWindowEvents()`; it diffs the previous frame's snapshot against a
freshly polled one and caches a single mouse delta. Key/mouse-button edge
detection (`keyPressed` / `keyReleased` / `mouseButtonPressed`) and
`computeMouseDelta` are also exported as free functions on `InputSnapshot`
values so the edge logic can be unit-tested without GLFW. Cursor capture is
toggled via `Input::setCursorCaptured(bool)`, which calls
`glfwSetInputMode` directly (not through `Window::setCursorMode`) and
flags the next frame's delta to zero so the OS-driven cursor jump is not
read as motion.

**Why.** Window already owns RAII for a GLFW handle; a separate `Input`
keeps polling state, edge tracking, and snapshot history out of Window
and gives a single seam to attach gamepad/rebinding later. Storing the
raw GLFW handle (rather than a `Window&` or `Window*`) keeps Input
movable without lifetime hazards — relevant because `App::make()` returns
`Result<App>` and the contained App is move-constructed once into the
`std::expected`. Splitting edge predicates and delta computation out as
free functions on the snapshot value type means tests don't need to
initialise GLFW. Driving cursor capture from `Input` directly avoids the
Window-pointer-into-App problem entirely.

The mouse delta is captured *once per render frame* even though the
free-fly camera reads it from `onFixedUpdate` (which may run multiple
sub-steps per frame). Mouse motion is an angular quantity (pixels per
frame), not a velocity, so re-applying the same delta across sub-steps
is the correct semantics — re-polling per sub-step would multiply look
sensitivity by the sub-step count.

**Where.** [`roboslop/src/platform/input.cppm`](../roboslop/src/platform/input.cppm),
[`roboslop/src/platform/window.cppm`](../roboslop/src/platform/window.cppm)
(adds `CursorMode` enum, `setCursorMode`, `handle()` accessor for Input),
[`roboslop/tests/input_test.cpp`](../roboslop/tests/input_test.cpp).

---

## 2026-05-17 — Camera is an ECS component picked by `ActiveCamera` tag

**Decision.** `roboslop::Camera` (module `roboslop.render.camera`) is an
ECS component whose `projection` field is a `std::variant<Perspective,
Orthographic>`. A separate empty-struct `ActiveCamera` tag marks which
camera-entity the renderer reads each frame. `findActiveCamera(world)` is
the public selection helper; `applyActiveCamera(world, viewId, w, h)`
reads the active camera, computes view+proj via glm
(`perspectiveRH_{ZO,NO}` / `orthoRH_{ZO,NO}` chosen by
`bgfx::getCaps()->homogeneousDepth`), and pushes them to bgfx with
`setViewTransform`. Per-draw model matrices come from `Transform` via
`bgfx::setTransform`, and the shader reads bgfx's built-in
`u_modelViewProj` — no custom MVP uniform.

**Why.** A camera-as-component plays naturally with future requirements:
robot-POV rendering for LLM-vision snapshots, split-screen, debug
free-cameras. The tag-based active selection avoids App-side state.
Using bgfx's idiomatic per-view + per-draw transform machinery (rather
than a hand-rolled `u_mvp` uniform) makes multiple views and instancing
cheaper later. Right-handed +Y-up -Z-forward matches glm's defaults so we
never reach for `bx::mtx*`.

**Where.** [`roboslop/src/render/camera.cppm`](../roboslop/src/render/camera.cppm),
[`roboslop/src/scene/transform.cppm`](../roboslop/src/scene/transform.cppm),
[`roboslop/src/render/mesh.cppm`](../roboslop/src/render/mesh.cppm)
(per-draw `setTransform`),
[`gorden/assets/shaders/src/vs_basic.sc`](../gorden/assets/shaders/src/vs_basic.sc)
(uses `u_modelViewProj`).

---

## 2026-05-17 — `AssetCache` owns `Program`s; `onSetup` receives a ref

**Decision.** `App` owns a `roboslop::AssetCache` (module
`roboslop.render.asset_cache`) that caches `Program`s keyed by
`(vsName, fsName)`. The cache hands out non-owning `ProgramHandle`s
storable on components. `AppConfig::onSetup`'s signature changes to
`Result<void>(World&, AssetCache&)` so game code requests programs
without holding handles itself. v1 is render-thread-only and has no
invalidation; both are documented in the module header.

**Why.** The first iteration of the game forced `main.cpp` to hold a
`Program` outside `App` so the bgfx handle survived until
`bgfx::shutdown()`. That wart blocks any second mesh entity (it would
need its own outside-`App` lifetime) and pushes lifecycle concerns into
game code where the engine should own them. Threading the cache into
`onSetup` is the minimal API extension — `onFixedUpdate` and `onRender`
don't need the cache (handles live in components by then), so their
signatures stay untouched.

**Where.** [`roboslop/src/render/asset_cache.cppm`](../roboslop/src/render/asset_cache.cppm),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm).
Supersedes the `onSetup` row of the 2026-05-17 callback-config entry
below.

---

## 2026-05-17 — App drives game code via callbacks on `AppConfig`

**Decision.** `AppConfig` carries three `std::function` hooks — `onSetup`,
`onFixedUpdate`, `onRender` — plus an `assetRoot` path. `App` owns the
loop, the `Window`, the `RenderContext`, and a `World`; the game's hooks
participate at well-defined moments. No virtual `GameApp` interface, no
engine-driven default render pipeline.

**Why.** A callback config is the lowest-ceremony API that still lets the
engine fix loop ordering and lifecycle. Virtual interfaces force a class
hierarchy the game doesn't need at this stage. Engine-driven default
pipelines hide control flow — keeping `onRender` empty by default and
letting the game opt into `submitMeshes(world)` keeps draw-call ordering
explicit and debuggable.

**Where.** [`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm),
hook documentation in [`docs/architecture.md`](architecture.md).

---

## 2026-05-17 — ECS facade: `World` exposes `forEach`, not entt views

**Decision.** `roboslop::World` (module `roboslop.ecs`) wraps
`entt::registry` and exposes `create`, `destroy`, `valid`, `emplace`,
`get`, `tryGet`, `has`, `remove`, and a templated `forEach<Components...>`
that takes a callback. It does **not** export `entt::basic_view`. Engine
subsystems that need iterator-level access (snapshot serialisation,
custom traversals) reach the underlying registry through `World::registry()`.

**Why.** Re-exporting `entt::basic_view` through a C++23 module loses the
non-member `operator==/!=` of entt's sparse-set iterator on the consumer
side — range-`for` does not compile in importing TUs without also
including entt headers, which defeats the purpose of the wrapper. A
custom view wrapper would lose entt's iteration ergonomics and
performance. `forEach` keeps the API self-contained without paying that
cost, since entt's `view::each` accepts callbacks and dispatches on
their signature.

**Where.** [`roboslop/src/ecs/world.cppm`](../roboslop/src/ecs/world.cppm).

---

## 2026-05-17 — `roboslop::App` + semi-fixed timestep from day 1

**Decision.** The engine ships an `App` class (module `roboslop.app`) as
the only entry point a game uses; `App::make()` returns `Result<App>` and
`App::run()` drives the loop. The loop is **semi-fixed** (Gaffer-style
accumulator) with a default 60 Hz fixed update and a variable render
slot. `FixedTimestep` clamps frames longer than 0.25 s.

**Why.** Physics (Jolt) requires a stable fixed step; switching the tick
model after subsystems already plug into it forces an API change across
every consumer. Introducing it now — while the fixed-update body is still
empty — is essentially free. `App` similarly anchors lifecycle so games
don't drive `init/tick/shutdown` themselves.

**Where.** [`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm),
[`roboslop/src/time/clock.cppm`](../roboslop/src/time/clock.cppm). Frame
loop overview in [`docs/architecture.md`](architecture.md).

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
