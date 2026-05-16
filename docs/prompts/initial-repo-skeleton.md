# Initial repo skeleton brief — Gorden (game) + Roboslop (engine)

## Your role and process

You are setting up the initial repository skeleton for a C++23 game project. This session is **not** about implementing gameplay or engine systems — it is about producing the documentation baseline, build-system skeleton, and conventions so future work has a stable foundation.

**Process you must follow:**

1. Read this entire brief.
2. Identify ambiguities. Pay special attention to the **OPEN** section at the end — those are areas where I deliberately want your proposal before you generate anything.
3. Ask all your clarifying questions in **one consolidated message**. Do not generate any files yet.
4. Wait for my answers.
5. Then generate the skeleton, working in logically atomic commits (see commit conventions below). Build configuration should be real and functional, not placeholder.
6. If new ambiguities emerge mid-work, stop and ask again rather than guessing.

If you find yourself wanting to make an architectural decision that this brief does not authorise, ask.

---

## Project overview

This is a monorepo with two artifacts living side by side:

- **`roboslop/`** — a game engine. Modern C++23, designed so that the world state can be efficiently described in text form and interacted with by an LLM. Engine-as-library; agnostic to the specific game built on top.
- **`gorden/`** — a single-player, top-down 3D game built on Roboslop. The central mechanic: the player has a robot companion played by an LLM. The robot "sees what the player sees" (mostly via semantic world snapshots, optionally augmented with rendered frames), takes actions through tool-calls, and can be customised by the player via modular upgrades and personality / system-prompt tuning.

The repo is private for now; licence undecided. Add a placeholder `LICENSE` file noting "All rights reserved — licence to be decided".

All file and directory names lowercase by default.

---

## Build & dependency requirements (verbatim — do not deviate)

- **Standard**: C++23. Set via `target_compile_features(<tgt> PRIVATE cxx_std_23)`. Prefer modern stdlib features (`std::print`, `std::expected`, `std::format`, `<ranges>`, `<chrono>`) over legacy equivalents when applicable.
- **Toolchain**: Latest stable Clang or GCC. Document the minimum supported version in the README.
- **Build system**: CMake, latest stable. Use `cmake_minimum_required(VERSION 3.30)` or newer. Strictly target-based — do not use directory-scoped commands (`include_directories()`, `add_definitions()`, `link_directories()`, `add_compile_options()`).
- **Generator**: Ninja.
- **Presets**: A `CMakePresets.json` at repo root is mandatory. Must define at minimum `debug`, `release`, `relwithdebinfo`, plus sanitizer presets (`asan-ubsan`, `tsan`). Configure, build, and test presets all defined.
- **Package manager**: Conan 2.x (latest stable). Dependencies declared in `conanfile.py` (preferred over `conanfile.txt`) using the `CMakeDeps` + `CMakeToolchain` generators. All versions pinned exactly — no ranges, no `[*]`.
- **Integration boundary**: `CMakeLists.txt` only uses standard `find_package(...)` + `target_link_libraries(... <ns>::<tgt>)`. No Conan-specific calls inside CMake. Conan is invoked separately and produces the toolchain file.
- **Targets**: Use `target_link_libraries`, `target_include_directories`, `target_compile_options`, `target_compile_features`, `target_compile_definitions` with explicit `PRIVATE` / `PUBLIC` / `INTERFACE` visibility. Header-only libraries as `INTERFACE` targets.
- **Layout**: Out-of-source builds only, under `build/<preset-name>/`. Source tree never written to.
- **Compile DB**: `CMAKE_EXPORT_COMPILE_COMMANDS=ON` always. Surface `compile_commands.json` at repo root (symlink) for clangd.
- **Lint / format**: `.clang-format` and `.clang-tidy` at repo root. Both enforced in CI; format check is blocking. (No CI yet — but configs in place.)
- **Warnings**: Baseline `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast` on GCC/Clang. `-Werror` in CI only, not in dev presets.
- **Testing**: CTest as the test driver. Test framework is a pinned Conan dependency.
- **Makefile**: A simple pipeline of targets to ease development.

### Resolved decisions on top of the above

- **Platforms**: prepare cross-platform from the start. Linux is primary, but Windows and macOS should not require structural changes later. Use generator expressions; avoid Linux-only assumptions in CMake / Conan / scripts.
- **Exceptions**: disabled project-wide (`-fno-exceptions`). Use `std::expected<T, E>` for fallible operations.
- **C++23 modules** from day one for new code. Header files only for code that genuinely cannot be a module yet (e.g. shimming a non-module dependency).
- **Test framework**: Catch2 v3 (Conan-pinned).
- **Logging**: spdlog (Conan-pinned).
- **Serialisation / data interchange**: `nlohmann/json` (Conan-pinned). Same representation used for LLM messages and save-games initially; swap later if needed.
- **Audio**: miniaudio (vendored or Conan-pinned, whichever is cleaner; single header).
- **Debug UI**: Dear ImGui with `imgui_impl_glfw` plus an ImGui-bgfx renderer. Gated by a `ROBOSLOP_DEV_UI` CMake option, default ON in `debug`/`relwithdebinfo` and OFF in `release`.
- **Shader pipeline**: build bgfx's `shaderc` and integrate as a CMake custom command. Each shader source produces variants for the relevant graphics backends. Source shaders under `gorden/assets/shaders/src/`, compiled outputs under `build/<preset>/shaders/`.
- **CI**: skipped for now. Provide a `Makefile` and `scripts/` with the workflows CI would later automate.

### Tech stack (engine dependencies)

- Windowing / input: **GLFW**
- Rendering: **bgfx**
- ECS: **EnTT**
- Asset loading: **Assimp** (3D models), **stb_image** (textures, if needed)
- Physics: **Jolt**
- Math: **glm**
- Logging: **spdlog**
- Audio: **miniaudio**
- Debug UI: **Dear ImGui**
- Serialisation: **nlohmann/json**
- Testing: **Catch2 v3**

All versions pinned exactly in `conanfile.py`. For libraries not available in a usable form on Conan Center (notably bgfx and miniaudio), propose your strategy (custom Conan recipe in `conan/recipes/`, FetchContent fallback, or vendor under `third_party/`) and let me pick before generating.

---

## Repo layout

Target structure (tweak the inside of `src/` to match the modules layout you propose):

```
.
├── CLAUDE.md
├── README.md
├── LICENSE
├── Makefile
├── CMakeLists.txt
├── CMakePresets.json
├── conanfile.py
├── .clang-format
├── .clang-tidy
├── .editorconfig
├── .gitignore
├── .gitattributes
├── cmake/
│   ├── CompilerWarnings.cmake
│   ├── Sanitizers.cmake
│   └── ShaderCompile.cmake
├── docs/
│   ├── architecture.md
│   └── conventions/
│       ├── commits.md
│       └── code-style.md
├── scripts/
│   ├── bootstrap.sh
│   ├── build.sh
│   ├── test.sh
│   ├── format.sh
│   └── tidy.sh
├── roboslop/
│   ├── CMakeLists.txt
│   ├── src/
│   └── tests/
├── gorden/
│   ├── CMakeLists.txt
│   ├── src/
│   ├── assets/
│   │   └── shaders/src/
│   └── tests/
└── third_party/                # only if strictly needed
```

---

## CLAUDE.md

**Keep it as small as possible.** Its job is to:

1. State that this file exists for AI agents working in the repo.
2. Insist that the file itself remains minimal — rules and conventions live in `docs/conventions/` and `docs/architecture.md`.
3. Link to:
   - `docs/conventions/commits.md`
   - `docs/conventions/code-style.md`
   - `docs/architecture.md`
4. Remind the agent: ask before assuming, work in atomic commits, run formatter and tidy before committing, do not add `Co-Authored-By` or similar trailers.

That is the entire scope of CLAUDE.md. Anything else belongs in `docs/`.

---

## Commit conventions → `docs/conventions/commits.md`

- **Format**: `scope: imperative subject line`
- **Subject length**: max 72 characters
- **Body**: not required; titles-only is the default. Bodies allowed when a change genuinely needs explanation.
- **Trailers**: none at this stage. No `Co-Authored-By`, no `Signed-off-by`, no `Refs:`, no issue references.
- **Scope examples**: `engine`, `game`, `build`, `docs`, `cmake`, `shaders`, `assets`, and subsystem scopes as they emerge (`ecs`, `render`, `physics`, `llm`, `input`, `audio`, …).
- **Atomicity**: each commit is a logically self-contained unit. Build and tests pass at each commit on `main`.
- **Branch model**: trunk-based. Commit directly to `main` unless instructed otherwise.
- **Examples**:
  - `build: add CMakePresets with debug, release, asan-ubsan, tsan`
  - `engine: scaffold roboslop module structure`
  - `docs: add architecture overview`

---

## Code-style conventions → `docs/conventions/code-style.md`

Go-inspired naming, adapted to C++:

| Kind | Convention | Example |
|---|---|---|
| Types (class, struct, enum, concept, type alias) | `PascalCase` | `EntityRegistry`, `ComponentMask` |
| Functions and methods | `camelCase` | `createEntity()`, `tickFrame()` |
| Variables, parameters, fields | `camelCase` | `entityCount`, `deltaSeconds` |
| Constants (`constexpr`, namespace-scope `const`) | `PascalCase` | `MaxEntityCount` |
| Namespaces | `lowercase`, single word | `roboslop`, `ecs` |
| Modules | `lowercase.dotted` | `roboslop.ecs.entity_registry` |
| Macros | `SCREAMING_SNAKE_CASE` | `ROBOSLOP_ASSERT` |
| Template parameters | `PascalCase` | `template<typename T, std::size_t N>` |
| File names | `lowercase_snake_case` | `entity_registry.cppm`, `entity_registry.cpp` |

**Language rules:**

- **No exceptions.** `-fno-exceptions`. Use `std::expected<T, E>` for fallible operations. A shared error type lives in `roboslop.core.error` (or wherever your modules layout proposal puts it).
- **Modules-first** for new code (`.cppm` for interface units). Headers only as a last resort.
- **Trailing return types** (`auto foo() -> T`) for new code. Consistency over dogma.
- **`[[nodiscard]]`** on functions returning `std::expected`, status flags, handles, or non-trivial values.
- **Namespace strategy**: each subsystem has its own namespace under the project root. Examples: `roboslop::ecs`, `roboslop::render`, `roboslop::llm`, `gorden::world`.
- **`.clang-format`**: LLVM base, modified to match this convention. Propose initial values for column limit, pointer alignment, brace style.
- **`.clang-tidy`**: enabled checks — `bugprone-*`, `modernize-*` (minus `use-trailing-return-type`), `performance-*`, `readability-*` (minus `identifier-length`), `cppcoreguidelines-*` (with pragmatic exclusions you propose), `misc-*`, `portability-*`, `concurrency-*`. Disabled families: Google, Fuchsia, LLVM, HICPP, Android, MPI, Obj-C, Zircon. Naming rules in clang-tidy must match the table above; configure them accordingly.

---

## Architecture document → `docs/architecture.md`

Short and structural. It will be edited heavily as the project evolves; aim for orientation, not detail. Cover at minimum:

### Engine / game separation

- `roboslop/` is a library. `gorden/` links it. The engine knows nothing about Gorden specifically.
- Public surface of the engine is its exported modules.

### Roboslop subsystem map

- ECS (EnTT)
- Rendering (bgfx) + shader pipeline (shaderc)
- Windowing & input (GLFW)
- Physics (Jolt)
- Math (glm)
- Assets (Assimp + stb_image)
- Audio (miniaudio)
- Logging (spdlog)
- Serialisation (nlohmann/json)
- Debug UI (Dear ImGui)
- **LLM** — first-class subsystem (see below)

### LLM integration (first-class engine subsystem)

- **Provider-agnostic.** Abstract `LLMBackend` interface. Concrete backends:
  - Anthropic
  - OpenAI
  - Local (Ollama / llama.cpp)
  - `NullBackend` — the game must remain fully playable without any LLM. Designs must degrade gracefully when no backend is configured.
- **Semantic-first perception** with hybrid option:
  - Primary path: the engine produces a structured world snapshot — visible entities and their components from the robot's perspective — serialised as JSON.
  - Optional augmentation: a rendered frame for multimodal backends that support image input.
- **Event-driven invocation.** The LLM is not on a fixed clock. It is invoked when meaningful world state changes or the player interacts with the robot. The cadence policy lives in the engine and is tunable.
- **Action interface: tool/function-calls only.** Examples: `moveTo`, `pickUp`, `say`, `inspect`. The engine validates and applies them; raw free-form text is not an executable action.
- **"Battery" / token-budget model.** Each LLM call draws from a configurable budget of tokens or API credits. The player sets thresholds and policies for what happens as the battery depletes. This is surfaced in-game as a property of the robot companion.
- **Robot customisation.**
  - Modular capabilities (perception range, memory size, available tools) that the player unlocks, buys, or finds in-world.
  - Personality and partial system-prompt tuning, exposed to the player at a controlled granularity.
  - Future direction: generative assets (models, plot lines) via LLM — Dwarf-Fortress-style emergent storytelling. Out of scope for the skeleton, but the architecture should not foreclose it.

### Gorden

- Top-down 3D. Outdoor, free movement is the primary mode.
- The robot mostly follows the player.
- Possibly additional modes / mini-games in the future — keep mode-switching as a concept the architecture can accommodate, but do not design it yet.

---

## Makefile and scripts

- `Makefile` is a thin wrapper that calls into `scripts/*.sh`. Targets are preset-aware where it makes sense (e.g. `make build PRESET=asan-ubsan`).
- `scripts/bootstrap.sh` should be idempotent: create the Conan profile if missing, run `conan install` for the chosen preset.
- All scripts should fail loudly on the first error (`set -euo pipefail`).

Confirm the target list with me before generating.

---

## OPEN — ask me about these before generating any files

1. **C++23 modules file layout.** Where module interface units (`.cppm`) live inside `roboslop/src/` and `gorden/src/`; how filenames map to module names; partition strategy (`module roboslop.ecs : entity` vs separate modules per file); whether to keep an `include/` directory at all for the module-era engine. Propose a concrete scheme with example paths.
2. **`.clang-format` starting values.** Column limit (proposal: 100), pointer alignment, brace style, namespace indentation, include grouping/sorting. Show your proposed config.
3. **`.clang-tidy` exclusions.** List the specific `cppcoreguidelines-*` and `modernize-*` checks you want to disable for game-dev pragmatics and explain each briefly. I want to approve the list before it lands.
4. **Conan profile strategy.** Where the profile lives (`conan/profiles/`?), whether `scripts/bootstrap.sh` generates a per-OS profile automatically, how it composes with `CMakePresets.json` (toolchain file location, multi-config vs single-config, profile-per-preset).
5. **Non-Conan-Center dependencies.** Specifically bgfx and miniaudio: custom Conan recipe under `conan/recipes/`, `FetchContent`, or vendor under `third_party/`? Same question for any other dependency you find is awkward on Conan Center.
6. **Makefile target set.** Propose the full list (e.g. `configure`, `build`, `test`, `format`, `tidy`, `clean`, `run-game`, `shaders`, `bootstrap`, `compdb`) and confirm preset semantics.
7. **`Error` type shape.** A single project-wide tagged enum, per-subsystem error enums combined at API boundaries, or `std::expected<T, std::string>` for early prototyping. Recommend one and justify.
8. **Pinned versions.** Propose exact versions for each Conan dependency. I want to see them before they land in `conanfile.py`.

When you have answers to all of these, propose the work plan (which files in which order, batched into commits), wait for my go-ahead, then execute.
