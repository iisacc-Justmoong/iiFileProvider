#pragma once
#include <array>
#include <cstddef>
#include <mutex>
#include <string_view>

namespace iiFileProvider::native::detail {
// Bound lock storage while allowing unrelated source identities to proceed.
// A collision only serializes the two identities; it cannot change a token.
class ChangeTokenCoordinator final {
public:
    static constexpr std::size_t stripeCount = 256;
    [[nodiscard]] static std::size_t stripe(std::string_view identity) noexcept;
    [[nodiscard]] std::unique_lock<std::mutex> lock(std::string_view identity);
private:
    std::array<std::mutex, stripeCount> stripes_;
};
}
