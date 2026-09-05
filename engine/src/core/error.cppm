module;
#include <expected>
#include <string>
#include <string_view>
export module roboslop.core.error;

export namespace roboslop {

struct Error {
    std::string_view category;
    int code;
    std::string_view message;
    std::string context;
};

template <typename T>
using Result = std::expected<T, Error>;

} // namespace roboslop
