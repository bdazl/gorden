module;

#include <entt/entt.hpp>

#include <cstddef>
#include <utility>

export module roboslop.ecs;

namespace roboslop {

export using Entity = entt::entity;
export inline constexpr Entity NullEntity = entt::null;

// Engine-side owner of an entt::registry. The wrapper keeps create/destroy/
// emplace/get/forEach behind a stable surface so game code never names
// entt types directly. Engine subsystems that need iterator-level access
// (snapshot serialisation, custom traversals) reach in via registry().
// See docs/decisions.md for why forEach was chosen over re-exporting
// entt::basic_view.
export class World {
  public:
    World() = default;

    World(const World&) = delete;
    auto operator=(const World&) -> World& = delete;
    World(World&&) noexcept = default;
    auto operator=(World&&) noexcept -> World& = default;
    ~World() = default;

    [[nodiscard]] auto create() -> Entity {
        return reg_.create();
    }

    auto destroy(Entity e) -> void {
        reg_.destroy(e);
    }

    [[nodiscard]] auto valid(Entity e) const noexcept -> bool {
        return reg_.valid(e);
    }

    // decltype(auto) so the return matches entt: T& for value components,
    // void for empty tag components (entt sparse-set-only storage).
    template <typename T, typename... Args>
    auto emplace(Entity e, Args&&... args) -> decltype(auto) {
        return reg_.emplace<T>(e, std::forward<Args>(args)...);
    }

    template <typename T>
    auto remove(Entity e) -> std::size_t {
        return reg_.remove<T>(e);
    }

    template <typename T>
    [[nodiscard]] auto get(Entity e) -> T& {
        return reg_.get<T>(e);
    }

    template <typename T>
    [[nodiscard]] auto get(Entity e) const -> const T& {
        return reg_.get<T>(e);
    }

    template <typename T>
    [[nodiscard]] auto tryGet(Entity e) -> T* {
        return reg_.try_get<T>(e);
    }

    template <typename T>
    [[nodiscard]] auto has(Entity e) const -> bool {
        return reg_.any_of<T>(e);
    }

    // Visit every entity that has all of Components in turn. Fn is called
    // with the lambda signature it advertises: either (Entity, Components&...)
    // or just (Components&...). entt::view::each dispatches at compile time.
    template <typename... Components, typename Fn>
    auto forEach(Fn&& fn) -> void {
        reg_.view<Components...>().each(std::forward<Fn>(fn));
    }

    template <typename... Components, typename Fn>
    auto forEach(Fn&& fn) const -> void {
        reg_.view<const Components...>().each(std::forward<Fn>(fn));
    }

    // Escape hatch for engine-internal subsystems that need raw registry
    // access (e.g. snapshot serialisation, custom iterators). Game code
    // should not call this.
    [[nodiscard]] auto registry() noexcept -> entt::registry& {
        return reg_;
    }

    [[nodiscard]] auto registry() const noexcept -> const entt::registry& {
        return reg_;
    }

  private:
    entt::registry reg_;
};

} // namespace roboslop
