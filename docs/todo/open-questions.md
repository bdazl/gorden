# Open questions for the build-system pass

These are the decisions deferred from the first repo-skeleton pass. Each entry
has a short statement of what is open, my proposed answer (so we have an anchor
to react to rather than starting from a blank page), and a `Decision (TBD)`
line for the user to fill in. When a decision lands, replace the TBD with the
chosen answer and link it from the relevant doc.

Until these are resolved, the following files are intentionally **not** in the
repo: `CMakeLists.txt`, `CMakePresets.json`, `conanfile.py`, `cmake/`,
`conan/`, `Makefile`, `scripts/`, `roboslop/`, `gorden/` (source trees),
`third_party/`, `.clang-format`, `.clang-tidy`.

---

## 1. C++23 module file layout

**What's open.** Where module interface units (`.cppm`) live inside
`roboslop/src/` and `gorden/src/`; how filenames map to module names; whether
to use partitions or separate modules per file; whether to keep an `include/`
directory at all in the module era.

**Proposal.** One module per logical unit; partitions only when a single
module is large enough to naturally split or when one interface has multiple
concrete implementations (e.g. LLM backends). No `include/` tree for internal
engine code; headers tolerated only as a shim layer under
`src/<subsystem>/shims/` for non-modularised dependencies. Filename →
module-name mapping:

```
roboslop/src/core/version.cppm           → module roboslop.core.version;
roboslop/src/core/error.cppm             → module roboslop.core.error;
roboslop/src/ecs/registry.cppm           → module roboslop.ecs.registry;
roboslop/src/ecs/registry.cpp            → module :impl;   (impl-unit for registry)
roboslop/src/render/renderer.cppm        → module roboslop.render.renderer;
roboslop/src/render/passes/forward.cppm  → module roboslop.render.passes.forward;
roboslop/src/llm/backend.cppm            → module roboslop.llm.backend;       (primary)
roboslop/src/llm/backend_anthropic.cppm  → module roboslop.llm.backend:anthropic;
roboslop/src/llm/backend_null.cppm       → module roboslop.llm.backend:null;
gorden/src/app/main.cpp                  → translation unit, imports modules
gorden/src/world/world.cppm              → module gorden.world;
```

CMake side: `target_sources(<tgt> PUBLIC FILE_SET cxx_modules TYPE CXX_MODULES FILES ...)`.

**Decision (TBD).**

---

## 2. `.clang-format` starting values

**What's open.** Column limit, pointer alignment, brace style, namespace
indentation, include grouping/sorting, indent width.

**Proposal.** LLVM base with the following overrides:

```yaml
BasedOnStyle: LLVM
Language: Cpp
Standard: c++20            # bump to c++23 once clang-format has a tag for it
ColumnLimit: 100
IndentWidth: 4
TabWidth: 4
UseTab: Never
PointerAlignment: Left          # int* p
ReferenceAlignment: Pointer     # follows pointer
NamespaceIndentation: None
FixNamespaceComments: true
ShortNamespaceLines: 1
BreakBeforeBraces: Attach       # K&R / LLVM style
AllowShortFunctionsOnASingleLine: Empty
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AlwaysBreakTemplateDeclarations: Yes
BinPackArguments: false
BinPackParameters: false
AlignAfterOpenBracket: BlockIndent
AllowAllArgumentsOnNextLine: true
AllowAllParametersOfDeclarationOnNextLine: true
IncludeBlocks: Regroup
IncludeCategories:
  - { Regex: '^<.*\.h(pp)?>$',  Priority: 1 }   # C system / 3rd-party header
  - { Regex: '^<.*>$',          Priority: 2 }   # stdlib
  - { Regex: '^"roboslop/.*"$', Priority: 3 }   # engine internal
  - { Regex: '^"gorden/.*"$',   Priority: 4 }   # game internal
  - { Regex: '.*',              Priority: 5 }
SortIncludes: CaseSensitive
SeparateDefinitionBlocks: Always
QualifierAlignment: Left
SpaceAfterTemplateKeyword: true
```

Open sub-questions: 4-space vs 2-space indent? `Attach` vs `Allman` braces?
Any missing include group?

**Decision (TBD).**

---

## 3. `.clang-tidy` exclusions

**What's open.** Which checks under the activated families to disable for
game-dev pragmatics, with per-check justification.

**Activated families (per brief).** `bugprone-*`, `modernize-*` (minus
`use-trailing-return-type`), `performance-*`, `readability-*` (minus
`identifier-length`), `cppcoreguidelines-*`, `misc-*`, `portability-*`,
`concurrency-*`.

**Proposed additional disables.**

| Check | Why |
|---|---|
| `cppcoreguidelines-avoid-magic-numbers`, `readability-magic-numbers` | Game code is full of tweakable constants; forcing `constexpr` names everywhere is noise. Re-enable selectively in hot paths. |
| `cppcoreguidelines-pro-bounds-pointer-arithmetic` | bgfx, GLFW, Assimp, etc. require pointer arithmetic at C-API bridges. |
| `cppcoreguidelines-pro-bounds-array-to-pointer-decay` | Same — C-API bridges. |
| `cppcoreguidelines-pro-type-reinterpret-cast` | Needed for GPU buffer upload, asset deserialisation, bgfx handles. |
| `cppcoreguidelines-pro-type-union-access` | EnTT internals and some SIMD / glm code use unions. |
| `cppcoreguidelines-non-private-member-variables-in-classes`, `misc-non-private-member-variables-in-classes` | ECS components are POD structs with public fields. |
| `cppcoreguidelines-owning-memory` | RAII + smart pointers, not the `gsl::owner` model. |
| `cppcoreguidelines-avoid-do-while` | Idiomatic in loop macros and scope guards. |
| `modernize-use-trailing-return-type` | Confirms brief exclusion. |
| `readability-identifier-length` | Confirms brief exclusion. |
| `readability-function-cognitive-complexity` | Too aggressive for ECS systems and render passes; revisit later. |
| `readability-named-parameter` | Forward decls and callback signatures read fine without parameter names. |
| `bugprone-easily-swappable-parameters` | More noise than value in math/geometry functions (`vec3` x/y/z). |
| `misc-no-recursion` | Scene-graph traversal and recursive descent are legitimate uses. |
| `misc-include-cleaner` | False positives against modules. Revisit once tooling matures. |

Naming options under `CheckOptions` are set to match
[`docs/conventions/code-style.md`](../conventions/code-style.md):
classes/structs/enums/concepts/type-aliases → `PascalCase`,
functions/methods → `camelBack`, variables/parameters/members → `camelBack`,
constexpr/global-constant → PascalCase, namespaces → `lower_case`, macros →
`UPPER_CASE`, template parameters → PascalCase.

**Decision (TBD).**

---

## 4. Conan profile strategy

**What's open.** Where Conan profiles live; whether `scripts/bootstrap.sh`
generates them automatically or picks pre-checked-in ones; how profiles
compose with `CMakePresets.json` (toolchain location, single- vs multi-config,
profile-per-preset vs profile-per-(OS,compiler)).

**Proposal.**

- Profiles checked in under `conan/profiles/`:
  `linux-clang-debug`, `linux-clang-release`, `linux-gcc-debug`,
  `linux-gcc-release`, `macos-clang-release`, `windows-msvc-release`.
  Sanitiser variants are reached by overriding settings, not by separate
  profiles.
- `scripts/bootstrap.sh` detects OS + compiler suite, **selects** (does not
  generate) the right profile, then runs:
  `conan install . --output-folder=build/<preset> --profile=conan/profiles/<x>
   --settings=build_type=<...> -s compiler.cppstd=23 --build=missing`.
- `CMakePresets.json` uses
  `"toolchainFile": "${sourceDir}/build/${presetName}/conan_toolchain.cmake"`
  and `"binaryDir": "${sourceDir}/build/${presetName}"`. Single-config Ninja,
  one build dir per preset.
- Mapping `preset → (profile, settings overrides)` lives in
  `scripts/bootstrap.sh`.

Open sub-question: profile-per-(OS,compiler) with overrides (proposal), or
strict profile-per-preset 1:1 (more files, more explicit)?

**Decision (TBD).**

---

## 5. Non-Conan-Center dependencies

**What's open.** Specifically bgfx, miniaudio, `imgui_impl_bgfx`: custom Conan
recipe, `FetchContent`, or vendored under `third_party/`.

**Proposal.**

| Dependency | Strategy | Why |
|---|---|---|
| **bgfx** (+ bx, bimg, shaderc) | **Custom Conan recipe** under `conan/recipes/bgfx/` | Three coupled libs that must build together; shaderc is a build-tool we invoke from CMake custom commands; non-trivial ABI. A recipe gives reproducibility + version pin. |
| **miniaudio** | **Vendor** under `third_party/miniaudio/` | Single header, no build, taken as an `INTERFACE` target. Packaging adds no value. |
| **imgui_impl_bgfx** | **Vendor** under `third_party/imgui_impl_bgfx/` with commit pin | No stable packaging exists; tiny (two files). |
| **stb_image** | Conan Center (`stb/cci.<rev>`) | Available, works. |
| glm, spdlog, fmt, nlohmann/json, Catch2, EnTT, GLFW, Assimp, Jolt, imgui | Conan Center | Modern versions all available. |

`third_party/CMakeLists.txt` exposes `INTERFACE` targets with include paths
and any required `target_compile_definitions`.

Open sub-questions: (a) Confirm strategy. (b) Specific bgfx commit/tag to pin?
Or pick latest stable at implementation time? (c) Specific
`imgui_impl_bgfx` fork/commit?

**Decision (TBD).**

---

## 6. Makefile target set

**What's open.** Full target list and preset semantics.

**Proposal.** All targets are preset-aware via `PRESET=…`, default `debug`.

| Target | What it does |
|---|---|
| `bootstrap` | `scripts/bootstrap.sh` — `conan install` for PRESET, writes the toolchain file. |
| `configure` | `cmake --preset $(PRESET)` (requires `bootstrap` first). |
| `build` | `cmake --build --preset $(PRESET) -j$(JOBS)`. |
| `test` | `ctest --preset $(PRESET) --output-on-failure`. |
| `run` | Run the game binary from `build/$(PRESET)/gorden/gorden`. |
| `shaders` | Compile shaders for PRESET (also pulled in by `build`). |
| `format` | `scripts/format.sh` — `clang-format -i` across `roboslop/`, `gorden/`. |
| `format-check` | `clang-format --dry-run --Werror` — the CI version. |
| `tidy` | `scripts/tidy.sh` — `clang-tidy` against `compile_commands.json`. |
| `compdb` | Refresh the `compile_commands.json` symlink at repo root. |
| `clean` | `rm -rf build/$(PRESET)`. |
| `distclean` | `rm -rf build/`. |
| `all` | `bootstrap configure build`. |
| `help` | List targets with short descriptions. |

Defaults: `PRESET ?= debug`, `JOBS ?= $(nproc)`. Default goal: `help`.

Open sub-questions: anything missing? Argument-passing for `run`
(`make run ARGS="..."`)? Should `build` auto-`bootstrap` if the toolchain
file is missing, or should it fail with a "run `make bootstrap` first" hint?

**Decision (TBD).**

---

## 7. `Error` type shape

**What's open.** Project-wide tagged enum vs per-subsystem enums combined at
API boundaries vs `std::expected<T, std::string>` for prototyping.

**Proposal.** Per-subsystem error enums + a lightweight wrapper type at API
boundaries:

```cpp
// roboslop/src/core/error.cppm
export module roboslop.core.error;

export namespace roboslop {

struct Error {
    std::string_view category;   // "io", "render", "llm", "asset"
    int code;                    // subsystem-defined
    std::string_view message;    // static string for built-in errors
    std::string context;         // optional dynamic detail (allocates only if used)
};

template <typename T>
using Result = std::expected<T, Error>;

} // namespace roboslop
```

Each subsystem defines its own `enum class FooError : int { ... }` and a
`toError(FooError) -> Error`. At the API boundary everything maps to
`roboslop::Error`.

**Why not the alternatives.** A single global enum forces `core` to know
about every subsystem and doesn't scale. `std::expected<T, std::string>` is
too proto: a string per error wastes both code and memory.

Open sub-question: accept the wrapper, or start with
`std::expected<T, std::string>` for prototyping and refactor at first real
need?

**Decision (TBD).**

---

## 8. Pinned Conan versions

**What's open.** Exact pinned versions for each Conan dependency.

**Proposal (as of 2026-05).**

| Package | Version | Note |
|---|---|---|
| `glfw` | `3.4` | |
| `entt` | `3.13.2` | |
| `glm` | `1.0.1` | |
| `spdlog` | `1.14.1` | pulls in `fmt/10.x` |
| `fmt` | `10.2.1` | pinned explicitly to keep `std::format`-shaped output stable |
| `nlohmann_json` | `3.11.3` | |
| `catch2` | `3.6.0` | |
| `assimp` | `5.4.2` | |
| `jolt-physics` | `5.1.0` | |
| `stb` | `cci.20240531` | |
| `imgui` | `1.90.8-docking` | docking branch — confirm before pinning |
| **bgfx** (custom recipe) | latest stable tag at implementation time | |
| **miniaudio** (vendored) | `0.11.21` | |
| **imgui_impl_bgfx** (vendored) | specific commit SHA | TBD — pick a fork |

Open sub-questions: bump anything to a newer release first? imgui docking vs
master?

**Decision (TBD).**

---

## 9. Minimum compiler versions to document

**What's open.** What we promise to support in the README.

**Proposal.** **Clang 18+** and **GCC 14+** — the lowest versions with
reasonable C++23 module support through CMake `FILE_SET cxx_modules`. MSVC
noted as "tracking", not validated, until someone actually runs it.

**Decision (TBD).**

---

## 10. README scope

**What's open.** Whether the README stays a minimal placeholder or carries
full prereqs + quickstart.

**Proposal.** Once the build pass lands, expand the README to: prereqs
(compiler versions, CMake ≥ 3.30, Ninja, Conan 2.x, Python ≥ 3.10),
quickstart (`make bootstrap configure build test`), and links into `docs/`.
No screenshots or marketing copy.

**Decision (TBD).**

---

## 11. `src/` skeleton — empty vs. hello-world

**What's open.** Whether the first commit of the source tree is an empty
skeleton or a minimal "hello world" that actually exercises the toolchain.

**Proposal.** Hello-world that proves the pipeline:

- `roboslop/src/core/version.cppm` exports `roboslop::version()`.
- `gorden/src/app/main.cpp` imports `roboslop.core.version` and prints via
  `std::print` + spdlog.
- A Catch2 test in `roboslop/tests/version_test.cpp`.

The alternative (empty skeleton) means we don't find out until later that the
module build, Conan integration, or Catch2 wiring is broken.

**Decision (TBD).**

---

## 12. Sample shaders for the shader-pipeline smoke test

**What's open.** Whether to ship minimal sample shaders so the bgfx
`shaderc` integration is actually exercised, or leave the pipeline as a
no-op until real shaders arrive.

**Proposal.** Include a minimal `vs_basic.sc` + `fs_basic.sc` +
`varying.def.sc` under `gorden/assets/shaders/src/` so the pipeline compiles
something end-to-end.

**Decision (TBD).**

---

## 13. `.editorconfig`, `.gitignore`, `.gitattributes` contents

**Status — resolved in the first pass.** What landed:

- `.editorconfig`: 4-space indent, UTF-8, LF, trim trailing whitespace, final
  newline. `Makefile` → tabs. `*.md` → no trim.
- `.gitignore`: `/build/`, `/compile_commands.json`, editor/IDE dirs,
  swap files, OS junk, `/.conan2/`.
- `.gitattributes`: `* text=auto eol=lf`; explicit `binary` for common image,
  model, and audio asset types. Shader sources (`*.sc`, `*.def.sc`) covered
  by the text default.

Re-open if any of those needs adjustment.

**Decision.** Landed in the documentation-baseline commits.

---

## 14. Push to remote vs. local-only commits

**What's open.** No remote is configured. Commits stay local until a remote
exists; once one is added, confirm whether to push automatically.

**Proposal.** Local-only for now. Ask before adding a remote or pushing.

**Decision (TBD).**

---

## 15. Commit batching for the build-system pass

**What's open.** Order of atomic commits for the second pass, once all of
the above are answered.

**Proposed order.** Each commit logically self-contained; build + tests pass
at every commit on `main`.

1. `style: add clang-format and clang-tidy configs`
2. `build: add top-level CMakeLists with compiler-warnings and sanitiser helpers`
3. `build: add CMakePresets with debug, release, relwithdebinfo, asan-ubsan, tsan`
4. `build: add conanfile.py with pinned dependencies`
5. `build: add conan profiles and bootstrap script`
6. `build: add Makefile and helper scripts (build/test/format/tidy/compdb)`
7. `build: add third_party with miniaudio and imgui_impl_bgfx (vendored)` — if chosen
8. `build: add custom Conan recipe for bgfx` — if chosen
9. `cmake: add shader-compile helper using bgfx shaderc`
10. `engine: scaffold roboslop module structure with core.version and core.error`
11. `engine: add Catch2 test target with version smoke test`
12. `game: scaffold gorden binary linking roboslop`
13. `shaders: add minimal basic vs/fs to verify shader pipeline` — if chosen

Open sub-question: would you like a pause after step 6 to actually run
`make bootstrap configure build` before continuing?

**Decision (TBD).**
