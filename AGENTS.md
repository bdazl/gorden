# Repository instructions for coding agents

These instructions apply to all coding agents working in this repository,
regardless of tool or model. Read the relevant documents below before making
changes. Keep this file minimal: detailed rules and conventions belong in
`docs/`.

## Project documentation

- [`docs/conventions/commits.md`](docs/conventions/commits.md) — commit format,
  atomicity, branch model.
- [`docs/conventions/code-style.md`](docs/conventions/code-style.md) — naming,
  language rules, formatter and linter expectations.
- [`docs/architecture.md`](docs/architecture.md) — engine / apps separation,
  application-driven principle, subsystem map, AI direction.
- [`docs/roadmap.md`](docs/roadmap.md) — milestones and open questions.
- [`docs/build-system.md`](docs/build-system.md) — toolchain, presets, profiles,
  sanitisers, dependency policy, C++23 module notes.
- [`docs/models.md`](docs/models.md) — Blender/glTF contract, model origins,
  bounds and correct placement on floors or other props.
- [`docs/save-format.md`](docs/save-format.md) — the save game format and
  where saves live.
- [`docs/agent-memory.md`](docs/agent-memory.md) — the robot's episodic
  memory, beliefs and goals, and what reaches the model.
- [`docs/decisions.md`](docs/decisions.md) — running log of design and tooling
  choices, newest first. Append new entries here; don't edit in place.

## Working conventions

- Ask before assuming. If the brief or existing docs do not authorise a
  decision, surface it instead of guessing.
- Work in **logically atomic commits**. Build and tests must pass at every
  commit on `main`.
- **Commit messages default to subject-only.** Write a body only when a
  change has a non-obvious rationale, a subtle invariant, or a trade-off
  worth recording — never to re-narrate the diff.
- Run the formatter and the linter before committing.
- Do **not** add `Co-Authored-By`, `Signed-off-by`, or any other trailers to
  commit messages.
- **Keep `docs/` in sync with the code.** When a change touches anything the
  docs describe (conventions, architecture, build system, decisions), update
  the relevant doc in the same commit — stale docs are worse than missing
  ones.
- Before placing an imported model, follow
  [`docs/models.md#model-origins-and-placement`](docs/models.md#model-origins-and-placement).
  Derive its transform from the exported model bounds and the intended support
  surface; do not assume the scene position is the bounds centre or that a
  primitive's transform position is its top surface.

## Local environment: running app windows

On this machine, the desktop is Hyprland with a Lua config. The user works
on workspaces 1 and up; put test windows on **workspace 9** and never touch focus or the
active workspace. `hyprctl dispatch` / `hyprctl keyword` fail on this config
("use eval"), and dispatchers that act on the active window hit the user's
windows, so always go through `hyprctl eval` with an explicit window handle:

```sh
# launch on a hidden workspace; the process is a child of Hyprland, so
# redirect output and record the exit status yourself
hyprctl eval 'hl.exec_cmd("cd '"$PWD"'/build/debug && apps/gorden/gorden > /tmp/gorden.log 2>&1; echo exit=$? >> /tmp/gorden.log", { workspace = "9 silent" })'

# control it by handle (class = window title: gorden, editor, shaderlab)
hyprctl eval 'local w = hl.get_window("class:gorden") hl.dispatch(hl.dsp.window.fullscreen({ mode = "fullscreen", window = w }))'
hyprctl eval 'local w = hl.get_window("class:gorden") hl.dispatch(hl.dsp.window.close({ window = w }))'
```

Keep test windows on workspace 9. In-app monitor fullscreen
(`glfwSetWindowMonitor`) moves the window onto the primary output and over
the user's screen. Resize or fullscreen it by handle instead.

Verify with `hyprctl activewindow -j` and `hyprctl activeworkspace -j` after
each step. Hidden windows still get configure events and render, so
fullscreen stress tests are valid there. `hyprctl eval` only prints `ok`;
to inspect the Lua API, write to a file with `io.open` from inside eval.
