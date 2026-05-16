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

## Language rules

- **No exceptions.** The project compiles with `-fno-exceptions`. Use
  `std::expected<T, E>` for fallible operations. A shared error type lives in
  `roboslop.core.error` (or wherever the modules-layout decision places it).
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

## Formatter and linter

`.clang-format` and `.clang-tidy` configurations are deferred to the
build-system pass; proposed values and the list of disabled checks are tracked
in [`docs/todo/open-questions.md`](../todo/open-questions.md). When the configs
land, both are enforced and the format check is blocking.
