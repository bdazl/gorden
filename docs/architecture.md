# Architecture

A structural overview for orientation, not a specification. The first
half describes **what exists today**; the second half describes the
**accepted direction** for the AI/agent side, which is documented here
before it is implemented so the coming vertical slices build on the same
vocabulary. Anything marked *open question* is deliberately still an
experiment.

## What Roboslop is

Roboslop is a platform, not a single game's engine. It hosts:

- a real-time rendering / game / simulation core (`engine/`),
- applications built on that core (`apps/`), of which Gorden is the
  first,
- tools around the engine (a shader lab and a level editor are planned),
- graphical experiments, and eventually adjacent libraries or "fusion
  projects" that reuse parts of the core.

The core is usable as a general game engine, but it is not designed as
an abstract universal engine. See the working principle below.

## Working principle: application-driven engine development

We work as a variant of "game-driven engine development". Because
Roboslop has several consumers rather than one game, we call it
**application-driven**:

> Do not build general engine abstractions because game engines
> "usually need them". Let concrete programs and experiments create the
> requirements.

In practice:

1. A concrete program needs a capability.
2. Implement the smallest good solution that makes *that program*
   useful. It may live in the app.
3. When the same concept shows up in several consumers, identify the
   shared abstraction.
4. Move it into the engine layer once the boundary has become real.

We specifically avoid building large generic subsystems up front for
hypothetical future needs. Roboslop generalises through concrete use,
not speculation. Corollary: a subsystem that exists in the engine today
is there because Gorden's demo scene needed it, and it is as small as
that need allowed.

## Repository layout

```text
/
├── engine/          # the roboslop library: src/, tests/, CMakeLists.txt
├── apps/
│   └── gorden/      # Gorden: game, gameplay/AI sandbox, engine demo
├── docs/
├── cmake/           # CMake helpers (modules, warnings, sanitisers, shaders)
├── conan/           # Conan profiles
├── third_party/     # FetchContent deps not on Conan Center
└── CMakeLists.txt
```

- **`engine/`** builds the static library target `roboslop`. Its public
  surface is its exported C++23 modules (`roboslop.*`), all in the
  `roboslop` namespace. The engine knows nothing about any particular
  app.
- **`apps/<name>/`** is one executable each, linking `roboslop`. App
  code owns everything specific to that app: scene content, gameplay
  rules, robot personalities, UI. New app directories are created when
  work on the app starts.
- Compiled shaders land at `build/<preset>/assets/shaders/<backend>/`;
  apps pass `assetRoot = "assets"` and `make run` executes from the
  build root so the relative path resolves.

## The applications

### Gorden

A future single-player top-down 3D game and, at the same time, our
gameplay/AI sandbox and the debug/demo app where new engine features
are tried first. It is acceptable for Gorden to look more like a
technical sandbox than a finished game for long stretches; that is part
of the strategy, not a failure of it.

The core game idea is stable: the player has a robot companion whose
high-level behaviour is decided by an AI/LLM. The robot perceives the
world mainly through structured, semantic observations, acts through
validated high-level tools, and never drives locomotion or physics
frame by frame. Today Gorden is a physics + textured-cube demo scene
with a free-fly camera; the agent loop does not exist yet (see the
[roadmap](roadmap.md), M2).

### Shader Lab (planned, M1)

A Shadertoy-like live shader environment that is not limited to a
fullscreen quad: edit shaders, hot-reload them, apply them to different
geometry (sphere, plane, imported mesh, terrain, fullscreen pass), move
around the scene, and see compile errors without losing the last
working program. It is the next engine-driving experiment and is
expected to surface runtime asset identity, shader recompilation, safe
render-thread resource replacement, diagnostics, and a dev UI.

### Level Editor (planned, M4)

A separate program for creating and editing scenes. It will drive
serialisation, asset identity, selection, gizmos, undo/redo, the
editing-vs-runtime state split, and physics/live preview. It is not
designed yet.

## Engine: current state

### Entry point and hooks

`roboslop::App` (module `roboslop.app`) is the bridge between an app and
the engine. An app constructs `App::make(AppConfig{...})` and calls
`run()`. `App` owns the window, render context, asset cache, world,
clock, scheduler, per-frame `FrameArena`, fixed-step `SystemGraph`,
`RenderGraph`, `JoltWorld`, and `AudioDevice`, and drives the frame loop
until the window closes.

App code participates through two callbacks on `AppConfig`:

| Hook | When | Signature |
|---|---|---|
| `onSetup` | Once, after init, before graph build | `Result<void>(World&, AssetCache&)` |
| `onBuildGraphs` | Once, after `onSetup`, before the loop | `void(SystemGraph&, RenderGraph&, FrameArena&)` |

`onSetup` seeds entities and loads assets. `onBuildGraphs` declares the
*systems* that run during fixed update and the *passes* that run during
render; there is no per-frame callback. Every frame the engine:

1. resets the arena;
2. polls window events and snapshots `Input`;
3. runs the fixed `SystemGraph` N times at the configured rate via the
   Taskflow executor;
4. runs `RenderGraph::execute` between `RenderContext::beginFrame()` and
   `endFrame()`, sequentially on the bgfx API thread.

### Frame loop

Semi-fixed timestep (Fiedler-style accumulator) in `roboslop.time.clock`:
default 60 Hz fixed rate, frames clamped at 0.25 s, leftover fraction
exposed as `alpha()` for render-time interpolation.

### Scheduling

`roboslop.sched`. A `SystemDesc` declares the resource ids it reads and
writes (opaque strings such as `"transforms"`, `"physicsState"`). The
scheduler derives an add-order-forward DAG from those declarations,
materialises it into one `tf::Taskflow` at compile time, and reuses it
every frame with no per-frame allocation. Subsystems that need
engine-owned state (Jolt world, audio device, light uniforms) park a
pointer in `entt::registry::ctx()` rather than adding typed fields to
`SystemCtx`, so `roboslop.sched` has no dependency on any subsystem.

### Platform

`roboslop.platform.window` and `roboslop.platform.input` wrap GLFW.
`roboslop.platform.process` runs a child process to completion and
captures its merged stdout/stderr (`runProcess`); it exists so tools such
as `shaderc` can be shelled out to from a worker thread. POSIX only —
other platforms get a `ProcessError::Unsupported` result rather than a
build break.

### Rendering

- `roboslop.render.context` owns bgfx init/shutdown and the frame.
- `roboslop.render.graph`: `PassDesc` + `RenderGraph`, same
  conflict-edge rule as systems, dense bgfx view-ID per pass.
- `roboslop.render.frontend`: `FrameArena` (single allocation,
  bump-pointer, reset per frame), flat `DrawItem`s, a packed 64-bit sort
  key (view class, view id, program, depth), and `collectMeshDraws` /
  `sortDraws` / `submitDraws`. No heap allocation in the render loop.
- `roboslop.render.camera`: `Camera` component with a
  perspective/orthographic variant, `ActiveCamera` tag, view/projection
  pushed through bgfx's per-view transforms.
- `roboslop.render.free_fly_camera`: debug camera as a component plus a
  pure tick function.
- `roboslop.render.lighting`: one `DirectionalLight` and a Lambert term
  in the textured fragment shader.
- `roboslop.render.shader`, `roboslop.render.mesh`,
  `roboslop.render.material`: program creation from compiled blobs
  (`makeProgram`) or from the asset root (`loadProgram`), static mesh
  creation with two vertex layouts, and a POD `Material` (program +
  albedo + sampler) the frontend reads per entity.
- Hot replacement of a program is a two-step swap on the render thread:
  `AssetCache::replaceProgram` exchanges the owning `Program` (bgfx
  defers the old handle's release to the end of the frame), then
  `rebindProgram` rewrites the handle copies held in `Mesh` / `Material`
  components. Components keep storing raw bgfx handles; there is no
  asset-identity indirection yet.

### Assets

`roboslop.assets.mesh` wraps Assimp (`loadMeshFile`),
`roboslop.assets.texture` wraps stb_image (`loadTexture2D`), and
`roboslop.assets.shader_compiler` wraps the `shaderc` executable
(`ShaderCompiler::compile`) for runtime recompilation — it spawns the
binary through `roboslop.platform.process` and returns the compiled blob
or shaderc's diagnostics text. `roboslop.core.file` holds the shared
whole-file reader.
`roboslop.render.asset_cache` caches programs, textures, samplers, and
uniforms and destroys them before bgfx shutdown. There is no asset
identity beyond file paths and no automatic invalidation; a caller that
recompiles a shader swaps it in explicitly via `replaceProgram`.

### Physics

`roboslop.physics` + `roboslop.physics.components`: a `JoltWorld` owned
by `App` and three fixed-update systems (`physicsSpawn`, `physicsStep`,
`syncPhysicsToTransform`) registered with one call. Bodies are described
by `BodyDesc` (sphere/box, static/dynamic). Jolt runs a single-threaded
job system so Taskflow is the only thread pool in the process.

### Audio

`roboslop.audio.device` owns one miniaudio engine; `roboslop.audio`
provides `AudioListener` / `AudioSource` components and the two systems
that update them. No sound source is spawned in the demo yet.

### Animation

`roboslop.animation.skeleton`, `.clip`, `.state`: skeleton and bone
transforms, per-channel keyframe tracks with heap-free `sampleClip`, and
an `AnimationState` component with a tick system. GPU skinning and rig
extraction from Assimp are not implemented.

### Subsystem map

| Subsystem | Library | State |
|---|---|---|
| ECS | EnTT | in use (`roboslop.ecs` facade) |
| System scheduling | Taskflow | in use |
| Rendering | bgfx (FetchContent) | in use |
| Shader pipeline | bgfx `shaderc` | build-time via CMake; runtime via `roboslop.assets.shader_compiler`, which shells out to the same binary |
| Windowing & input | GLFW | in use |
| Physics | Jolt | in use |
| Math | glm | in use |
| Assets — 3D models | Assimp | in use |
| Assets — textures | stb_image | in use |
| Audio | miniaudio (FetchContent) | in use |
| Logging | spdlog | in use (a handful of call sites) |
| Serialisation | nlohmann/json | declared in Conan, not yet linked |
| Debug UI | Dear ImGui (docking) | declared in Conan, not yet linked; `ROBOSLOP_DEV_UI` option exists but gates nothing yet |
| LLM / agent runtime | — | not started; direction below |

## Accepted direction: AI and agents

Nothing in this section exists in code. It is written down now so that
M2 and M3 (see the [roadmap](roadmap.md)) share one vocabulary and do
not each invent their own.

### Semantic-first perception

The agent primarily receives an **engine-produced, structured
observation** of the world from the robot's point of view. Rendered
images may be used as multimodal augmentation for backends that accept
them, but they are not the canonical world-state channel.

Exactly which facts a robot may observe is an *open question* for
experiments. We deliberately avoid locking in a broad or god-like
perception model now; the first `Observation` should contain what the
first tools need and nothing more.

### High-level actions

The LLM is a decision maker, not a low-level controller. The
conceptual pipeline is:

```text
World
  ↓
Observation
  ↓
Agent
  ↓
Proposed Tool Call
  ↓
Validation
  ↓
Command / Intent
  ↓
Simulation
  ↓
Events
```

Appropriate action granularity is `moveTo(target)`, `pickUp(entity)`,
`inspect(entity)`, `say(...)`. The engine/simulation owns pathfinding,
locomotion, animation, and physics. `Observation`, `ToolCall`,
`Command`/`Intent`, and `Event` are kept conceptually separate even
while the first implementations are tiny: a tool call is a *proposal*
until validation turns it into a command, and events are what the
simulation reports back, not what the agent asked for.

### Local-first, asynchronous inference

The primary target is local inference. The provider abstraction may
grow remote backends later, but the architecture must not assume an
external API service is always present. Inference is asynchronous
relative to the simulation: the game loop never blocks while the model
thinks. Results arrive as proposed tool calls to be validated on the
simulation side.

### Reflection opportunities

The agent has no fixed "think tick". Instead the simulation emits
event-driven opportunities: player interaction, a meaningful world
event, a tool failure, goal completion, a memory trigger, or a generic
`ReflectionOpportunity` — a voluntary chance for internal activity when
nothing demands immediate action.

Internal activity is expressed through explicit mechanisms (update goal,
store memory, revise belief, inspect memory) rather than by making
private free text or chain-of-thought persistent gameplay state.

### Memory is a first-class concept

The robot's memory is not "chat history" or a token count. We separate:

| Layer | Meaning |
|---|---|
| **World truth** | What the simulator knows to be true. |
| **Perception / observations** | What the robot actually observed. |
| **Working memory** | The bounded active context assembled for one inference call. |
| **Episodic memory** | Events the robot remembers ("Anna asked me at the bridge to find the generator"). |
| **Semantic memory / beliefs** | Facts or opinions the robot holds. May be incomplete, stale, wrong, or based on what an NPC said. |
| **Goals** | The agent's explicit active intentions and sub-goals. |
| **Retrieval** | The mechanism that decides which old memories become relevant again. |

World truth and robot belief are explicitly not the same thing. This
makes memory both AI infrastructure and potential gameplay: future robot
upgrades can be larger episodic memory, better retrieval, better
perception, more tools, or more reflection opportunities.

The exact data model and any embedding/vector-store technology are
*open questions*. Provenance for beliefs is an accepted direction,
roughly:

```text
belief:
    subject
    predicate
    value
    source
    learned_at
    confidence
```

so that we can later answer "why does the robot believe this?".

### Replay and reproducibility

We want to debug and replay AI-driven sessions. The goal is **not** that
the same initial state makes the LLM produce identical output again;
nondeterminism, and the robot becoming its own thinker, is part of the
point. The goal is to replay the **observed and validated event and
action sequence**, e.g.

```text
RobotObserved(...)
AgentToolCall(moveTo(...))
ToolAccepted
RobotArrived(...)
AgentToolCall(inspect(...))
```

In replay mode, previously accepted actions are fed back into the
simulation without asking the model. For debugging we also want to save
observation, prompt, and model response, but those are analysis data,
not the authoritative replay signal. Full determinism of physics or the
whole engine is not required by this design.

## Out of scope for now

Earlier documents described a "battery / token budget" model, a
provider list including specific remote vendors, and robot
customisation mechanics. These remain possible gameplay ideas but are
not part of the accepted architecture until an app needs them; they are
recorded in the history of [`decisions.md`](decisions.md), not here.
