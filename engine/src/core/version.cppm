module;
#include <string_view>
export module roboslop.core.version;

export namespace roboslop {

constexpr auto version() noexcept -> std::string_view {
    return "0.0.1";
}

} // namespace roboslop
