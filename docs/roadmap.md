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

**Open questions.**

- Does Shader Lab compile shaders by shelling out to the `shaderc`
  binary or by linking it in? Shelling out is simpler and keeps the
  compiler out of the process; linking gives better diagnostics.
- Should shader *sources* live under the app's asset root or under a
  shared `assets/` at the repo root once more than one app needs them?
- File watching: polling `last_write_time` versus a platform watcher.
  Polling is enough for the first slice.

---

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

**Open questions.**

- Which local runtime do we target first (llama.cpp server, Ollama,
  an in-process library)? Pick the one with the lowest integration cost
  that still supports structured tool-call output.
- How rich is the first `Observation`? Start with what the three tools
  need and nothing else.
- Where does the tool-call validation live — engine or app? Start in
  the app; move to the engine once Shader Lab or the editor wants the
  same shape.

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

**Goal.** The first concrete editor use case. Not "build Unity".

Enough to: open or create a small scene, select an entity, manipulate
its transform, save, load, and start/preview the scene.

Engine needs this is expected to surface: scene/world serialisation,
asset identity (shared with Shader Lab), selection, transform gizmos,
undo/redo, editing versus runtime state, physics preview, possibly live
preview of a running scene.

**Open questions.**

- Is the editor a separate executable, or a mode of the app it edits?
  A separate executable is the working assumption.
- Serialisation format for scenes (JSON via nlohmann is already a
  declared dependency; a binary format may follow).
- How much of the editor's UI is shared with Shader Lab's dev UI?

---

## Beyond M4

Not planned in detail. Candidates: GPU skinning and rigged characters
(the animation data layer already exists), multiple lights, a proper
character controller for the robot, remote LLM providers behind the
same abstraction, and further "fusion projects" that reuse the engine
core.
