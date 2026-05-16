# Architecture

A short, structural overview. This document is expected to evolve heavily as
the project grows; the goal here is orientation, not detail.

## Engine / game separation

- **`roboslop/`** is a library. **`gorden/`** links against it. The engine
  knows nothing about Gorden specifically.
- The public surface of the engine is its exported modules.
- Anything game-specific (player loadouts, robot personalities, level layout,
  story content) lives in `gorden/`.

## Roboslop subsystem map

| Subsystem | Library |
|---|---|
| ECS | EnTT |
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
