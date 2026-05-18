# Architecture

A short, structural overview. This document is expected to evolve heavily as
the project grows; the goal here is orientation, not detail.

## Engine / game separation

- **`roboslop/`** is a library. **`gorden/`** links against it. The engine
  knows nothing about Gorden specifically.
- The public surface of the engine is its exported modules.
- Anything game-specific (player loadouts, robot personalities, level layout,
  story content) lives in `gorden/`.

## Engine entry point

`roboslop::App` (module `roboslop.app`) is the bridge between a game and the
engine. A game constructs `App::make(AppConfig{...})`, then calls `run()`;
`App` owns the window, render context, asset cache, world, clock,
scheduler, the per-frame `FrameArena`, the fixed-step `SystemGraph`, and
the `RenderGraph`. It drives the frame loop until the window closes.

### App hooks

Game code participates through two callbacks on `AppConfig`:

| Hook | When | Signature |
|---|---|---|
| `onSetup` | Once, after init, before graph build | `Result<void>(World&, AssetCache&)` |
| `onBuildGraphs` | Once, after `onSetup`, before the loop | `void(SystemGraph&, RenderGraph&, FrameArena&)` |

`onSetup` seeds entities and loads assets. `onBuildGraphs` declares the
*systems* that run during fixed-update and the *passes* that run during
render — there is no per-frame callback. Every frame, the engine
executes the compiled graphs:

1. `arena.reset()`
2. `Window` polls events; `Input` snapshots a new frame
3. For each fixed sub-step at the configured rate, `Scheduler::run`
   executes `fixedGraph` (a `SystemGraph`) on the Taskflow executor.
4. `RenderContext::beginFrame()` → `RenderGraph::execute` (sequential,
   on the bgfx API thread) → `RenderContext::endFrame()`.

A `SystemDesc` declares the resource ids it reads and writes — opaque
strings like `"transforms"`, `"physicsState"`, `"drawItems"`. The
scheduler derives an add-order-forward DAG from those declarations and
materialises it into a `tf::Taskflow` once at compile time; per-frame
execution does no Taskflow allocation. The same conflict-edge rule
governs `PassDesc`s in the `RenderGraph`, where each pass is assigned a
dense bgfx view-ID in add-order.

`onSetup` receives a reference to the App-owned `AssetCache`. Game code
requests programs via `assets.program(vsName, fsName)` and stores the
returned `ProgramHandle` on components; the cache keeps the underlying
`Program` alive until App destruction (which happens before bgfx
shutdown).

`AppConfig::assetRoot` points the engine at the game's runtime asset
directory; shader loaders (`roboslop::loadProgram`) and the `AssetCache`
resolve paths relative to it.

## Frame loop

The engine drives a **semi-fixed timestep** (Glenn Fiedler, "Fix Your
Timestep!"): wall-clock dt is accumulated by `roboslop::FixedTimestep`,
which emits N fixed sub-steps per frame at a configurable rate (default
60 Hz). Frames longer than 0.25 s are clamped so a debugger pause does
not trigger a "spiral of death". Render runs once per frame; the leftover
accumulator fraction is exposed as `alpha() ∈ [0, 1)` for interpolation
between fixed states when subsystems begin to use it.

The renderer is split into **frontend** and **backend** halves. The
frontend (`roboslop.render.frontend`) walks the ECS once per pass and
emits flat `DrawItem`s into the per-frame `FrameArena` — a single-
allocation bump pointer reset at frame start — then sorts the span in
place. The backend (`submitDraws`) iterates the sorted span and calls
bgfx. No heap allocation runs in the render loop. The sort key bakes
view-class, view-ID, program, and depth into a single `uint64` so a
single `std::sort` does all draw-call ordering.

## Roboslop subsystem map

| Subsystem | Library |
|---|---|
| ECS | EnTT |
| System scheduling | Taskflow |
| Rendering | bgfx |
| Shader pipeline | bgfx `shaderc` (via CMake custom command) |
| Windowing & input | GLFW |
| Physics | Jolt |
| Math | glm |
| Assets — 3D models | Assimp |
| Assets — textures | stb_image (when needed) |
| Audio | miniaudio |
| Logging | spdlog |
| Serialisation | nlohmann/json |
| Debug UI | Dear ImGui (`imgui_impl_glfw` + ImGui-bgfx renderer) |
| **LLM** | first-class engine subsystem (see below) |

Debug UI is gated by a `ROBOSLOP_DEV_UI` CMake option, default ON in `debug`
and `relwithdebinfo` and OFF in `release`.

## LLM integration

The LLM is a first-class engine subsystem, not a bolt-on.

### Provider-agnostic backend interface

An abstract `LLMBackend` interface, with concrete backends:

- Anthropic
- OpenAI
- Local (Ollama / llama.cpp)
- `NullBackend`

**The game must remain fully playable without any LLM configured.** Designs
that route any gameplay-critical signal through the LLM must degrade
gracefully when `NullBackend` is active.

### Semantic-first perception

- **Primary path:** the engine produces a structured world snapshot — the
  visible entities and their components from the robot's perspective —
  serialised as JSON.
- **Optional augmentation:** a rendered frame for multimodal backends that
  support image input. Snapshots remain the canonical channel; vision is
  additive.

### Event-driven invocation

The LLM is not on a fixed clock. It is invoked when meaningful world state
changes or the player interacts with the robot. The cadence policy lives in
the engine and is tunable.

### Action interface

Tool / function-calls only. Examples: `moveTo`, `pickUp`, `say`, `inspect`.
The engine validates and applies each call; raw free-form text is not an
executable action.

### Battery / token-budget model

Each LLM call draws from a configurable budget of tokens or API credits. The
player sets thresholds and policies for what happens as the battery depletes.
This is surfaced in-game as a property of the robot companion.

### Robot customisation

- **Modular capabilities** — perception range, memory size, available tools —
  unlocked, bought, or found in-world.
- **Personality and partial system-prompt tuning,** exposed to the player at
  a controlled granularity.
- **Future direction (out of scope for the skeleton, but not foreclosed):**
  generative assets (models, plot lines) via LLM — Dwarf-Fortress-style
  emergent storytelling.

## Gorden gameplay

- Top-down 3D. Outdoor, free movement is the primary mode.
- The robot mostly follows the player.
- Additional modes / mini-games may exist in the future. Mode-switching is
  retained as a concept the architecture can accommodate, but is not yet
  designed.
