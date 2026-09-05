# Code-style conventions

## Naming

Go-inspired naming, adapted for C++:

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

Private members follow the same `camelCase` rule — no trailing `_`, no
`m_` prefix. When an accessor method would collide with the bare member
name, give the accessor a descriptive name (`glfwHandle()`,
`bgfxHandle()`, `framebufferWidth()`) or remove it if nothing calls it.
`.clang-tidy` enforces this via
`readability-identifier-naming.PrivateMemberSuffix: ""`.

## Language rules

- **Expected failures are values, not exceptions.** Fallible operations
  return `std::expected<T, E>` (`roboslop::Result<T>`); the shared error type
  lives in `roboslop.core.error`. Exceptions are compiled *in* (compiler
  default) but are not used as control flow in engine or app code; they are
  tolerated where third-party code throws and where nothing better exists.
- **Modules-first.** New code lives in C++23 module interface units (`.cppm`).
  Header files are a last resort, reserved for shimming non-module
  dependencies.
- **Trailing return types.** Use `auto foo() -> T` for new code. Consistency
  over dogma.
- **`[[nodiscard]]`** on functions returning `std::expected`, status flags,
  handles, or other non-trivial values.
- **Modern stdlib first.** Prefer `std::print`, `std::format`, `std::expected`,
  `<ranges>`, `<chrono>` over older equivalents.

## Namespaces

Each subsystem has its own namespace under the project root. Examples:

- `roboslop::ecs`
- `roboslop::render`
- `roboslop::llm`
- `gorden::world`

## Module layout

C++23 modules are the default unit of code organisation. Interface units use
`.cppm`; impl units use `.cpp` and start with `module :impl;`. Filename and
module-name mapping:

| Filesystem path | Module name |
|---|---|
| `engine/src/core/version.cppm`             | `roboslop.core.version` |
| `engine/src/render/passes/forward.cppm`    | `roboslop.render.passes.forward` |
| `engine/src/llm/backend_null.cppm`         | `roboslop.llm.backend:null` |
| `apps/gorden/src/world/world.cppm`         | `gorden.world` |

Rules:

- **One module per logical unit.** Use partitions (`module foo:bar`) only
  when a single interface has multiple concrete implementations (e.g. LLM
  backends).
- **No `include/` tree for internal engine code.** Headers are tolerated only
  under `src/<subsystem>/shims/` as a bridge to non-modular C-APIs (bgfx,
  GLFW, Assimp, ...).
- **`import std;` stays off** until libc++/libstdc++ ship a usable `std`
  module. Inside a `.cppm`, use the global module fragment
  (`module;` + `#include <print>` etc.) for stdlib headers.
- **Partitions live inside a single target.** A partition cannot be served
  from a separate library; if a future variant needs its own shared library,
  it becomes a sibling module instead.

The build side is in [`docs/build-system.md`](../build-system.md); the CMake
helper is `roboslop_add_module_library()` in
[`cmake/Modules.cmake`](../../cmake/Modules.cmake).

## Error handling

Fallible operations return `std::expected<T, Error>` via the alias
`roboslop::Result<T>`. The error type is a lightweight struct defined once in
[`roboslop.core.error`](../../engine/src/core/error.cppm):

```cpp
struct Error {
    std::string_view category;   // e.g. "io", "render", "llm"
    int              code;       // subsystem-defined
    std::string_view message;    // static description for built-in errors
    std::string      context;    // optional dynamic detail
};
```

Each subsystem owns its own `enum class FooError : int` plus a
`toError(FooError) -> Error` mapping; `Error` is what crosses API boundaries.

Sample:

```cpp
import roboslop.core.error;

auto loadConfig(std::string_view path) -> roboslop::Result<Config> {
    if (path.empty()) {
        return std::unexpected(roboslop::Error{
            .category = "config",
            .code     = 1,
            .message  = "empty path",
        });
    }
    // ...
}
```

Exception support is left at the compiler default. The policy is a
convention, not a flag: engine and public APIs express expected failures
(asset not found, shader compile failed, window init failed, invalid config)
as `Result`, never by throwing. Third-party code is not special-cased for
exceptions, and no target adds `-fno-exceptions` or exception-disabling
defines for dependencies. Any deliberate exception use (e.g. catching a
throwing third-party API at a boundary) should be local and commented.

## Formatter and linter

[`.clang-format`](../../.clang-format) and [`.clang-tidy`](../../.clang-tidy)
at the repo root are the source of truth. The rationale behind the chosen
settings is in [`docs/decisions.md`](../decisions.md).

- `make format` rewrites files in place.
- `make format-check` is the dry-run CI mode.
- `make tidy` runs `clang-tidy` against `compile_commands.json`
  (regenerated by `make compdb`).

`clang-tidy` on `.cppm` files is best-effort while module tooling matures in
LLVM 22; run it primarily on `.cpp` impl units. `misc-include-cleaner` is
disabled project-wide for the same reason.
