# Roadmap

Direction, not a contract. Milestones are ordered by what we want to
learn next, and each one is meant to be a *vertical slice* that pulls a
real requirement through the engine — see the application-driven
principle in [`architecture.md`](architecture.md). Dates are
deliberately absent. Decisions taken while executing a milestone are
logged in [`decisions.md`](decisions.md); this file only says where we
are heading.

Legend for status words used below: **current state** is what the tree
does today, **accepted direction** is what we have agreed to build, and
**open question** is something we are deliberately leaving to
experiment.

---

## M0 — Roboslop platform reset

**Goal.** Reposition the repository from "Gorden, a game on the Roboslop
engine" to "Roboslop, a platform with Gorden as one application".

- Rename the project and establish the `engine/` + `apps/` layout.
- Rewrite README and architecture to describe the current truth.
- Document application-driven engine development as the working
  principle.
- Document the AI direction: semantic-first perception, high-level
  actions, local-first inference, reflection opportunities, memory as a
  first-class concept, replay of validated actions.
- Make the C++ modules and exceptions policies pragmatic.

**Definition of done.** A new developer can read README + architecture +
roadmap and understand what Roboslop is, what Gorden is, why several
apps exist, how engine features are expected to emerge, and which AI
architecture we are aiming at.

**Status.** Done with this milestone's commits.

---

## M1 — Shader Lab: live shader development

**Goal.** The first new Roboslop application: a Shadertoy-like sandbox
that is *not* limited to a fullscreen quad. It is the next big
engine-driving experiment.

Vertical slice:

1. Start the app and pick at least one simple geometry (a sphere or
   plane is enough to begin with).
2. Apply a shader program to it and move the camera around.
3. Edit the shader source on disk; the app detects the change.
4. Recompile through `shaderc`, then replace the running program
   hygienically on the render thread.
5. On a compile failure, show the diagnostics and keep the last working
   program alive.

Engine needs this is expected to surface: runtime asset identity,
shader recompilation, safe hot replacement of GPU resources,
diagnostics surfacing, geometry/camera inspection, and a dev UI where
it is actually needed (this is where the ImGui slot in `third_party/`
finally gets filled).

Later Shader Lab targets, not part of the first slice: imported meshes,
terrain, fullscreen passes, multiple geometry types side by side.

**Explicitly avoided.** Designing a complete generic asset system first.
Build what Shader Lab needs; generalise when Gorden or the editor needs
the same thing.

**Status.** First vertical slice done (2026-09-05): `apps/shaderlab`
renders a sphere and a plane, watches the shader sources, recompiles
through the `shaderc` binary on a worker thread, swaps the program on
the render thread, and shows diagnostics in an ImGui panel while the
last working program stays live. The "later targets" above remain open.

**Resolved questions** (details in [`decisions.md`](decisions.md)).

- Runtime compilation shells out to the `shaderc` binary; linking it in
  stays an option if diagnostics or latency become a problem.
- Shader sources live under each app's `assets/shaders/`; the engine's
  own ImGui pair lives under `engine/assets/shaders/`. No shared
  top-level `assets/` yet.
- File watching polls `last_write_time` at 250 ms, app-side.

**Open questions.**

- Asset identity: programs are still raw bgfx handles copied into
  components and rewritten on swap. Revisit when the editor (M4) or a
  second hot-reloaded asset type needs a stable id.

---

## Cross-cutting: developer tooling

Not a milestone, but the milestones lean on it. Exists today: the dev
UI with a window registry (View menu, F1, persisted layout and
visibility), per-app settings under the XDG config dir, an in-memory
virtual filesystem with live and host mounts (`roboslop.vfs`), a small
shell over it (`roboslop.shell`), and a terminal window
(`roboslop.ui.terminal`). Gorden exposes its agent log, observation,
transcript, status, and settings as files. Candidates: a monospace
font, a diegetic role for the terminal inside the game, mounting Shader
Lab's diagnostics.

## M2 — Gorden agent vertical slice

**Goal.** The minimal real AI/gameplay loop, proving the agent/runtime
boundary rather than building a game.

```text
player + robot
    ↓
semantic observation
    ↓
local LLM backend
    ↓
a few high-level tools (moveTo, inspect, say)
    ↓
validation
    ↓
simulation
```

What lands: an `Observation` produced by the engine from the robot's
point of view, a provider abstraction with one *local* backend, a tool
schema for a handful of actions, a validation layer that turns a
proposed `ToolCall` into a `Command`, and the simulation-side execution
of those commands (pathfinding/locomotion can be trivial to start with).

Inference must be asynchronous relative to the fixed-update loop from
the first version; the game loop never blocks on the model.

**Status.** First slice done (2026-09-05): `roboslop.llm` with a
scripted backend and an OpenAI-compatible HTTPS backend; in Gorden the
`gorden_agent` library (observation, tools + validation, kinematic
locomotion, `AgentBrain`) and a chat panel. Verified with the scripted
provider in tests and against OpenAI (`gpt-4.1-mini`).

**Resolved questions** (details in [`decisions.md`](decisions.md)).

- Runtime: one OpenAI-compatible backend, verified against OpenAI first;
  a llama.cpp server is the same backend with another base URL. The
  local-first direction is unchanged, the order of verification is not.
- First `Observation`: robot and player positions, named entities within
  a radius with distance, recent events, the player's message. Nothing
  else.
- Validation lives in the app (`gorden.agent.tools`), as planned.

**Open questions.**

- When a second agent-driven app appears, which of observation building,
  validation rules, and the brain's event loop move into the engine.
- Line-of-sight and other perception limits: today every named entity
  in range is visible.

---

## M3 — Agent memory, reflection, replay

**Goal.** First real version of the memory model and the replay
machinery described in [`architecture.md`](architecture.md).

- Episodic memory, beliefs with provenance, goals.
- Retrieval that picks which old memories become relevant again.
- Reflection opportunities as event-driven prompts for internal work
  (update goal, store memory, revise belief, inspect memory).
- Event/action logging of the *validated* sequence.
- Replay mode that feeds previously accepted actions back into the
  simulation without asking the model again.

Keep the implementation small enough to observe and debug by hand.

**Open questions.**

- Storage for episodic/semantic memory: plain structs in a vector, a
  small embedded store, or embeddings? Start without embeddings.
- How much of the memory model is engine (reusable by any agent-driven
  app) versus Gorden-specific? Unknown until a second consumer exists.
- What is the unit of replay — one log per session, per agent, or per
  world?

---

## M4 — Level editor vertical slice

**Status.** First primitive-scene slice implemented ahead of M3 (2026-09-06):
`apps/editor` creates, selects and transforms objects, edits solid materials
and a directional light, saves/loads versioned JSON, provides undo/redo, and
previews physics with restoration on Stop. Gorden consumes the same scene
document. See [scene editing](scene-editor.md) for controls and limits.

**Goal.** The first concrete editor use case. Not "build Unity".

Enough to: open or create a small scene, select an entity, manipulate
its transform, save, load, and start/preview the scene.

Engine needs this is expected to surface: scene/world serialisation,
asset identity (shared with Shader Lab), selection, transform gizmos,
undo/redo, editing versus runtime state, physics preview, possibly live
preview of a running scene.

**Open questions.**

- The editor is a separate executable; JSON version 1 is the first scene format.
- Hierarchy, imported models, prefabs and richer asset identity remain open.
- How much of the editor's UI is shared with Shader Lab's dev UI?

---

## Beyond M4

Not planned in detail. Candidates: GPU skinning and rigged characters
(the animation data layer already exists), multiple lights, a proper
character controller for the robot, remote LLM providers behind the
same abstraction, and further "fusion projects" that reuse the engine
core.
