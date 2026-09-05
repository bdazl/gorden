# Commit conventions

## Format

Each commit subject is:

```
scope: imperative subject line
```

- **Maximum subject length:** 72 characters.
- **Body:** optional. Title-only is the default. Include a body only when a
  change genuinely needs explanation — a non-obvious rationale, a subtle
  invariant, a trade-off worth recording.
- **Trailers:** none at this stage. Do not add `Co-Authored-By`,
  `Signed-off-by`, `Refs:`, or issue references.

## Scopes

Use whichever fits best:

- `engine` — anything in `engine/`
- `gorden`, `shaderlab` — anything in `apps/gorden/` / `apps/shaderlab/`
  (each app is its own scope, named after its directory)
- `apps` — changes spanning several apps or the `apps/` layout itself
- `build` — CMake, Conan, presets, scripts
- `cmake` — CMake-specific helpers under `cmake/`
- `docs` — anything under `docs/`, plus the top-level README and CLAUDE.md
- `shaders` — under `apps/<app>/assets/shaders/`
- `assets` — non-shader assets
- `repo` — repo-wide hygiene (editor configs, `.gitignore`, `.gitattributes`)
- Subsystem scopes as they emerge: `ecs`, `render`, `physics`, `llm`
  (`roboslop.llm` and its backends), `input`, `audio`, `ui`, ...

## Atomicity

Each commit is a self-contained logical unit. **Build and tests must pass at
every commit on `main`.** A commit that leaves the tree broken is not atomic.

## Branch model

Trunk-based. Commit directly to `main` unless instructed otherwise. There are
no long-lived feature branches at this stage.

## Examples

- `build: add CMakePresets with debug, release, asan-ubsan, tsan`
- `engine: scaffold roboslop module structure`
- `docs: add architecture overview`
