# CLAUDE.md

This file exists for AI coding agents working in this repository. It is
deliberately tiny: rules and conventions live in `docs/`, not here. Keep this
file minimal — new rules go in `docs/`.

## Where the rules live

- [`docs/conventions/commits.md`](docs/conventions/commits.md) — commit format,
  atomicity, branch model.
- [`docs/conventions/code-style.md`](docs/conventions/code-style.md) — naming,
  language rules, formatter and linter expectations.
- [`docs/architecture.md`](docs/architecture.md) — engine / game separation and
  subsystem map.
- [`docs/build-system.md`](docs/build-system.md) — toolchain, presets, profiles,
  sanitisers, dependency policy, C++23 module notes.
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
