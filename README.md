# Gorden

A single-player, top-down 3D game built on **Roboslop**, a custom C++23 game
engine designed for first-class LLM integration. The central mechanic is a
robot companion, played by a large language model, that perceives the world
through structured snapshots, acts via tool-calls, and can be customised by
the player through modular upgrades and personality tuning.

This repository is a monorepo:

- `roboslop/` — the engine, in library form. Engine-as-library; game-agnostic.
- `gorden/` — the game built on top of it.

## Status

Skeleton in progress. The documentation baseline is in place; the build
system, source tree, and tooling configs are tracked in
[`docs/todo/open-questions.md`](docs/todo/open-questions.md) and land in
follow-up passes.

## Documentation

- [Architecture overview](docs/architecture.md)
- [Commit conventions](docs/conventions/commits.md)
- [Code-style conventions](docs/conventions/code-style.md)
- [Original brief](docs/prompts/initial-repo-skeleton.md)
- [Open questions for the build-system pass](docs/todo/open-questions.md)

## Licence

All rights reserved — licence to be decided. See [`LICENSE`](LICENSE).
