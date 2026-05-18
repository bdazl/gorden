# Decisions

A running log of design and tooling choices for Gorden/Roboslop. Newest
first. Each entry stays tight: what was decided, why, and where it lives.
If a choice is later changed, append a new entry that supersedes the old
one — don't edit in place.

---

## 2026-05-18 — Audio: miniaudio AudioDevice + 3D listener/source systems

**Decision.** `roboslop.audio.device` exposes a move-only `AudioDevice`
that owns one `ma_engine`. `roboslop.audio` exports `AudioListener`
(tag), `AudioSource` (POD with `ma_sound*` + gain),
`updateAudioListener` / `updateAudioSources` free functions, plus
`registerAudioSystems(SystemGraph&)` that wires them into the
fixed-update graph. `installAudioDevice(world, device)` parks the
device pointer in entt's ctx storage so the systems reach it without
SystemCtx coupling — same pattern as `installJoltWorld`.

The miniaudio implementation TU is a `.c` file
(`roboslop/src/audio/miniaudio_impl.c`), the single place that
`#define`s `MINIAUDIO_IMPLEMENTATION`. Compiling it as C dodges
`-fno-exceptions` and old-style-cast warnings that miniaudio's
internals would otherwise trip on a C++ compile. The project's
`LANGUAGES` were widened to `CXX C` for this one file.

`third_party/CMakeLists.txt` marks miniaudio's include as a `SYSTEM`
interface so the engine's strict warning set
(`-Wold-style-cast`, `-Wsign-conversion`, ...) does not flag the
header in modules that just declare against it.

App owns one `AudioDevice` after `JoltWorld` in declaration order.
Gorden's demo adds an `AudioListener` tag to the camera entity and
calls `registerAudioSystems(fixed)` — no sound source yet, but the
listener pose tracks the free-fly camera and the engine path is
hot.

**Why.** miniaudio's C API stays in one TU so the C/C++ ABI boundary
is one file thick. The same `ctx`-storage pattern as physics avoids
forcing every subsystem onto `SystemCtx`; the scheduler stays free
of audio-typed dependencies. Strict warnings stay on for engine code
but lifted for a single third-party impl file — exactly where they
add no value.

**Where.** [`roboslop/src/audio/audio_device.cppm`](../roboslop/src/audio/audio_device.cppm),
[`roboslop/src/audio/audio.cppm`](../roboslop/src/audio/audio.cppm),
[`roboslop/src/audio/miniaudio_impl.c`](../roboslop/src/audio/miniaudio_impl.c),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm) (owns
AudioDevice; calls installAudioDevice),
[`third_party/CMakeLists.txt`](../third_party/CMakeLists.txt)
(SYSTEM include), [`CMakeLists.txt`](../CMakeLists.txt) (LANGUAGES
CXX C), [`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp)
(AudioListener on camera, `registerAudioSystems`).

---

## 2026-05-18 — Forward directional lighting with one Lambert pass

**Decision.** `roboslop.render.lighting` exposes `DirectionalLight`
(POD: direction, color, intensity), the pure `packDirectionalLightUniform`
helper (returns the two vec4s the shader binds — normalised dir +
intensity, color), `uploadDirectionalLight(World&, dirUniform,
colorUniform)` which finds the first light in the world and pushes
its uniforms, and `LightUniforms` (a bundle of uniform handles).

`AssetCache` grows a `uniform(name, type)` accessor that caches
non-sampler bgfx uniforms with the same destruction schedule as the
sampler cache.

The textured fragment shader (`fs_textured.sc`) now computes
`max(dot(N, -L), 0) * intensity * color * albedo` plus a 0.15
constant ambient. Vertex-coloured entities (the M1 cubes/spheres)
remain on `fs_basic.sc` and are unaffected.

The gorden demo seeds one `DirectionalLight` entity and stashes a
`LightUniforms` in `world.registry().ctx()`; the main render pass
reads the uniforms from ctx and uploads them every record.

**Why.** A single directional light is the minimum to show a textured
object has shape. Routing light uniforms through ctx-storage rather
than capturing them in the pass-record lambda keeps the lambda's
captured state stable across App moves and matches the pattern
already used for `JoltWorld`. Two separate `vec4` uniforms (not one
mat or array) keep the bgfx call sites trivial; once we have
multiple lights, an array uniform replaces them.

**Where.** [`roboslop/src/render/lighting.cppm`](../roboslop/src/render/lighting.cppm),
[`roboslop/src/render/asset_cache.cppm`](../roboslop/src/render/asset_cache.cppm)
(uniform() accessor),
[`gorden/assets/shaders/src/fs_textured.sc`](../gorden/assets/shaders/src/fs_textured.sc)
(Lambert pass), [`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp)
(light entity + ctx wiring). Tests:
[`roboslop/tests/lighting_test.cpp`](../roboslop/tests/lighting_test.cpp).

---

## 2026-05-18 — Asset pipeline: Assimp meshes, stb_image textures, Material component

**Decision.** Three new asset/render modules plus an `AssetCache`
extension:

- `roboslop.assets.mesh` exposes `MeshVertex` (pos+normal+uv POD),
  `MeshAsset`, and `loadMeshFile(path)` via Assimp.
- `roboslop.assets.texture` exposes an RAII `Texture` wrapper and
  `loadTexture2D(path)` via stb_image. The single
  `STB_IMAGE_IMPLEMENTATION` TU is `roboslop/src/assets/stb_image_impl.cpp`.
- `roboslop.render.material` exposes the POD `Material` component
  (program + albedo + sampler-uniform handles).

`AssetCache` grows two caches: `texture(path) -> bgfx::TextureHandle`
and `sampler(name) -> bgfx::UniformHandle`. Both ride App's
destruction order (cache dies before bgfx::shutdown). The cache's
move-ops moved from defaulted to hand-written because samplers need
explicit `bgfx::destroy` calls, and we want the moved-from cache to
be empty (not still owning handles).

The render frontend's `collectMeshDraws` now checks for a `Material`
component via `try_get`: when present, it overrides `Mesh.program`
with `Material.program` and propagates the albedo+sampler handles
into the `DrawItem`. `submitDraws` issues `bgfx::setTexture(0,
sampler, albedo)` only when both handles are valid, so vertex-coloured
draws (no Material) keep working unchanged.

New vertex layout `vertexLayoutPosNormalUv()` matches `MeshVertex`,
and the new shader pair `vs_textured.sc` / `fs_textured.sc` reads
albedo from `s_albedo`. `varying.def.sc` was extended with `v_normal`,
`v_texcoord0`, `a_normal`, `a_texcoord0`.

`submitMeshes(World&)` (the M0-era direct-submission helper) is
removed; the frontend/backend path now owns every draw.

**Why.** The plan called for "thin slice per milestone". The MVP
exercises the *engine* path end-to-end — Texture/Mesh loaders compile
and link against Assimp / stb_image — without committing checked-in
binary assets to the repo. The gorden demo builds a procedural
4×4 checkerboard texture and a 24-vertex pos+normal+uv cube inline,
running them through the same Material → DrawItem → submitDraws
pipeline that an Assimp `.obj` + PNG load will use unchanged. Real
file-loading is exercised the moment we commit a mesh; nothing in
the engine needs to change.

Routing sampler uniforms through `AssetCache::sampler(name)` (rather
than letting game code call `bgfx::createUniform` directly) keeps
their lifetime co-located with the textures they bind; otherwise
bgfx complains at shutdown about a leaked uniform. Keeping the
non-textured pos+color path alive means the M1 vertex-coloured
physics demo still works while the M2 textured cube falls alongside.

**Where.** [`roboslop/src/assets/mesh_loader.cppm`](../roboslop/src/assets/mesh_loader.cppm),
[`roboslop/src/assets/texture_loader.cppm`](../roboslop/src/assets/texture_loader.cppm),
[`roboslop/src/assets/stb_image_impl.cpp`](../roboslop/src/assets/stb_image_impl.cpp),
[`roboslop/src/render/material.cppm`](../roboslop/src/render/material.cppm),
[`roboslop/src/render/asset_cache.cppm`](../roboslop/src/render/asset_cache.cppm)
(texture + sampler caches),
[`roboslop/src/render/frontend.cppm`](../roboslop/src/render/frontend.cppm)
(Material-aware draw collection),
[`roboslop/src/render/mesh.cppm`](../roboslop/src/render/mesh.cppm)
(adds `vertexLayoutPosNormalUv()`; drops `submitMeshes`).
Shaders: [`gorden/assets/shaders/src/vs_textured.sc`](../gorden/assets/shaders/src/vs_textured.sc),
[`fs_textured.sc`](../gorden/assets/shaders/src/fs_textured.sc),
extended [`varying.def.sc`](../gorden/assets/shaders/src/varying.def.sc).
Build: [`conanfile.py`](../conanfile.py) already required `assimp/5.4.2`
and `stb/cci.20230920`; [`roboslop/CMakeLists.txt`](../roboslop/CMakeLists.txt)
links `assimp::assimp` + `stb::stb`.

---

## 2026-05-18 — Jolt physics integrated as a fixed-update subsystem

**Decision.** `roboslop.physics` exposes `JoltWorld` (move-only value
type, single-instance per process), the engine's three physics layer
filters (single-instance concrete implementations of Jolt's virtual
interfaces, hidden in `namespace detail`), and the three physics
systems registered into the fixed-update SystemGraph via
`registerPhysicsSystems(SystemGraph&)`:

- `physicsSpawn` — turns `BodyDesc` components into Jolt bodies, swaps
  in `RigidBody` and seeds `PrevTransform`.
- `physicsStep` — calls `system->Update(dt, 1, &tempAlloc, &jobSystem)`
  with `JPH::JobSystemSingleThreaded`.
- `syncPhysicsToTransform` — copies pose back to ECS `Transform`,
  capturing the prior pose into `PrevTransform`.

App owns one `JoltWorld` and calls `installJoltWorld(world, joltWorld)`
once at the start of `run()`, placing the pointer in entt's
ctx-storage. Physics systems reach the JoltWorld via
`SystemCtx.world->registry().ctx().get<JoltWorld*>()` rather than a
typed member on `SystemCtx`, so `roboslop.sched` keeps zero
dependencies on subsystem modules.

`roboslop.physics.components` is the POD-only component module:
`BodyShape` (`std::variant<SphereShape, BoxShape>`), `BodyMotion`,
`BodyDesc`, `RigidBody` (wraps `JPH::BodyID`), `PrevTransform`.

**Why.** The plan called for an MVP-thin Jolt slice (rigid bodies,
sphere + box, dynamic + static, no character controller). Putting the
JoltWorld pointer in `entt::registry::ctx()` rather than `SystemCtx`
avoids forcing `roboslop.sched` to declare physics types — a non-
exported forward declaration in a different C++23 module raised a
duplicate-class error from clang. The ctx-storage approach is the
opposite trade-off (untyped lookup at runtime) but keeps the
dependency graph clean: every subsystem that later wants engine-
owned state can register through the same ctx mechanism.

`JobSystemSingleThreaded` rather than `JobSystemThreadPool` keeps
Taskflow as the only thread-pool in the process — Jolt steps are
serialised in the SystemGraph via the `"physicsState"` write
declaration, so the cost of single-threading the solver shows up only
if the solver itself is the bottleneck. Re-evaluate when we feel it.

`-fno-exceptions` is preserved: Jolt's public surface returns error
codes or invalid handles rather than throwing, and `JPH::Ref<Shape>`
ref-counts shape lifetimes without unwind-table assumptions.

**Where.** [`roboslop/src/physics/jolt_world.cppm`](../roboslop/src/physics/jolt_world.cppm),
[`roboslop/src/physics/components.cppm`](../roboslop/src/physics/components.cppm),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm) (declares
JoltWorld member, calls `installJoltWorld`),
[`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp) (M1 demo:
3 dynamic boxes + 2 dynamic spheres + 1 static ground).
Tests: [`roboslop/tests/physics_test.cpp`](../roboslop/tests/physics_test.cpp).
Build: [`conanfile.py`](../conanfile.py) already required
`joltphysics/5.2.0`; [`roboslop/CMakeLists.txt`](../roboslop/CMakeLists.txt)
links `Jolt::Jolt`.

---

## 2026-05-18 — System scheduling via Taskflow + add-order-forward DAGs

**Decision.** Two new module clusters land before any engine
fundamentals (Jolt, Assimp, lighting, audio, animation):

- `roboslop.sched` exposes `SystemDesc { name; reads; writes; run }`,
  `SystemGraph` (which materialises an add-order-forward DAG into a
  `tf::Taskflow`), and `Scheduler` (which wraps one `tf::Executor`,
  held behind a `unique_ptr` so the Scheduler itself stays movable).
- `roboslop.render.graph` exposes `PassDesc { name; reads; writes;
  record }` and `RenderGraph::execute` — sequential record on the bgfx
  API thread, dense view-ID assignment in add-order.
- `roboslop.render.frontend` exposes `FrameArena` (single
  ctor-allocation, bump-pointer, reset per frame), the flat `DrawItem`
  POD, `makeSortKey` (viewClass / viewId / program / depth packed into
  one `u64`), `collectMeshDraws`, `sortDraws`, `submitDraws`.

`AppConfig` drops the per-frame `onFixedUpdate` and `onRender`
callbacks. Game code instead supplies `onBuildGraphs(SystemGraph&,
RenderGraph&, FrameArena&)` — called once before the loop. The engine
compiles the SystemGraph after the callback returns and reuses the
same `tf::Taskflow` every frame; per-frame execution does no Taskflow
allocation. `App::run()` resets the arena at frame start, runs the
fixed graph N times per frame at the configured rate, then drives the
render graph once between bgfx begin/endFrame.

Resource ids are opaque strings (`"transforms"`, `"physicsState"`,
`"drawItems"`, `"framebuffer"`). The scheduler does not introspect
them. Edges are derived between every (earlier, later) add-order pair
that shares any of write/write, write/read, or read/write — meaning
the DAG is by construction acyclic, no cycle check needed.

**Why.** The user's mandate for the engine-fundamentals MVP chain was
**system-of-systems with parallel-friendly scheduling from day 1** and
**a render-graph + frontend/backend split with zero allocations in the
render loop**. Both needed to be in place before the five milestones
(physics, assets, lighting, audio, animation) land, so individual
subsystems can register their systems and passes without re-architecting
later. Taskflow on Conan-Center gives us a header-only work-stealing
executor; routing all systems through it means the migration to a
fully-parallel engine is a matter of declaring more resource ids
correctly, not rewriting plumbing.

Add-order-forward edges (as opposed to all-pairs) keep the graph a DAG
by construction and match how a hand-written game-loop reads top-to-
bottom — registering systems in dependency order matches dependency
order in execution. `-fno-exceptions` is preserved: we validate input
ourselves and never trigger Taskflow's throwing paths.

The frontend/backend split lives in `frontend.cppm` as free functions
over POD `DrawItem`s in a `FrameArena`. bgfx submission stays on the
bgfx API thread (`RenderGraph::execute` is sequential); CPU work
inside a pass can fan out via `tf::Subflow` later when a hotspot
warrants it. View-IDs are assigned in add-order and capped at 256 (a
bgfx limit, asserted by `RenderGraph::add`).

**Where.** [`roboslop/src/sched/scheduler.cppm`](../roboslop/src/sched/scheduler.cppm),
[`roboslop/src/render/graph.cppm`](../roboslop/src/render/graph.cppm),
[`roboslop/src/render/frontend.cppm`](../roboslop/src/render/frontend.cppm),
[`roboslop/src/app/app.cppm`](../roboslop/src/app/app.cppm),
[`gorden/src/app/main.cpp`](../gorden/src/app/main.cpp).
Build: [`conanfile.py`](../conanfile.py) requires `taskflow/3.7.0`;
[`roboslop/CMakeLists.txt`](../roboslop/CMakeLists.txt) links
`Taskflow::Taskflow`. Tests:
[`roboslop/tests/scheduler_test.cpp`](../roboslop/tests/scheduler_test.cpp),
[`roboslop/tests/render_graph_test.cpp`](../roboslop/tests/render_graph_test.cpp),
[`roboslop/tests/frame_arena_test.cpp`](../roboslop/tests/frame_arena_test.cpp).
Supersedes the per-frame-callback rows of the 2026-05-17
callback-config entry below.

---

## 2026-05-18 — Private members drop the trailing-underscore suffix

**Decision.** Private members follow the same `camelCase` rule as every
other field — no `_` suffix, no `m_` prefix. Where an accessor method
would share its name with the renamed member, the accessor is renamed
to a descriptive form: `Window::handle() → glfwHandle()`,
`Program::handle() → bgfxHandle()`,
`RenderContext::width()/height() → framebufferWidth()/framebufferHeight()`.
Accessors with no callers (`Window::cursorMode()`, `App::input()`,
`App::world()`, `App::assets()`, `App::config()`) were deleted outright
rather than renamed.

**Why.** The trailing underscore was an unwritten C++ habit, never
documented in the convention table or enforced by tidy. It contradicted
the Go-inspired naming row that says "Variables, parameters, fields |
`camelCase`". Renaming brings members into line, and renaming/removing
the colliding accessors keeps the public API expressive (`glfwHandle`
says more than `handle`) instead of leaning on a suffix as a tiebreaker.
The ctor-parameter shadowing case in `App::App(Window window, ...)`
where `input(window)` would read the moved-from parameter is resolved by
writing `input(this->window)` — members are initialised in declaration
order, so `this->window` is the just-constructed member at that point.

**Where.** Convention: [`docs/conventions/code-style.md`](conventions/code-style.md).
Enforcement: [`.clang-tidy`](../.clang-tidy) gains
`PrivateMemberSuffix: ""`. Affected modules: `app.cppm`, `input.cppm`,
`window.cppm`, `context.cppm`, `asset_cache.cppm`, `shader.cppm`,
`clock.cppm`, `world.cppm` plus the one call-site in
`gorden/src/app/main.cpp`.

---

## 2026-05-18 — Free-fly debug camera is an ECS component + pure tick

**Decision.** `roboslop::FreeFlyCamera` (module
`roboslop.render.free_fly_camera`) is an ECS component holding accumulated
yaw/pitch (radians, Euler), `moveSpeed`, `boostMultiplier`, and
`lookSensitivity`. The system `updateFreeFlyCameras(World&, Input&, dt)`
packs the current `Input` state into a `FreeFlyTickInput` value and
dispatches to the pure helper `tickFreeFlyCamera(ctrl, transform, in, dt)`
for each entity that has both a `FreeFlyCamera` and a `Transform`.
Right-mouse-button held = active (capture cursor + read motion); released
= idle (the controller skips the tick). Pitch is clamped just inside ±90°
so the forward vector never aligns with world-up. Diagonal motion is
normalised so WASD + Space don't produce a √3× speed boost.

**Why.** Matches the existing `Camera`/`ActiveCamera` pattern — controllers
are components, not free-standing classes. Storing yaw/pitch on the
controller (rather than reading them back from `Transform.rotation`) keeps
the pitch clamp authoritative and avoids the lossy quat→Euler round-trip
near the poles. Exposing a pure `tickFreeFlyCamera` over a hand-built
`FreeFlyTickInput` lets the math be unit-tested without `Input`/`World`/
GLFW. The system's RMB capture/release uses the new
`mouseButtonReleased` edge predicate so the toggle is exact (no
re-asserting state every frame, which would reset the mouse-delta).

**Where.** [`roboslop/src/render/free_fly_camera.cppm`](../roboslop/src/render/free_fly_camera.cppm),
[`roboslop/tests/free_fly_camera_test.cpp`](../roboslop/tests/free_fly_camera_test.cpp).

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
