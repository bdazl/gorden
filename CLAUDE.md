# CLAUDE.md

This file exists for AI coding agents working in this repository. It is
deliberately tiny: rules and conventions live in `docs/`, not here. Keep this
file minimal — new rules go in `docs/`.

## Where the rules live

- [`docs/conventions/commits.md`](docs/conventions/commits.md) — commit format,
  atomicity, branch model.
- [`docs/conventions/code-style.md`](docs/conventions/code-style.md) — naming,
  language rules, formatter and linter expectations.
- [`docs/architecture.md`](docs/architecture.md) — engine / apps separation,
  application-driven principle, subsystem map, AI direction.
- [`docs/roadmap.md`](docs/roadmap.md) — milestones and open questions.
- [`docs/build-system.md`](docs/build-system.md) — toolchain, presets, profiles,
  sanitisers, dependency policy, C++23 module notes.
- [`docs/save-format.md`](docs/save-format.md) — the save game format and
  where saves live.
- [`docs/decisions.md`](docs/decisions.md) — running log of design and tooling
  choices, newest first. Append new entries here; don't edit in place.

## Reminders

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

## Running app windows on this machine

The desktop is Hyprland with a Lua config. The user works on workspaces 1
and up; put test windows on **workspace 9** and never touch focus or the
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

Verify with `hyprctl activewindow -j` and `hyprctl activeworkspace -j` after
each step. Hidden windows still get configure events and render, so
fullscreen stress tests are valid there. `hyprctl eval` only prints `ok`;
to inspect the Lua API, write to a file with `io.open` from inside eval.
