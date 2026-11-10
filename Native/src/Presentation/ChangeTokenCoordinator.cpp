#include "ChangeTokenCoordinator.h"
#include <functional>

namespace iiFileProvider::native::detail {
std::size_t ChangeTokenCoordinator::stripe(std::string_view identity) noexcept {
    return std::hash<std::string_view>{}(identity) % stripeCount;
}
std::unique_lock<std::mutex> ChangeTokenCoordinator::lock(std::string_view identity) {
    return std::unique_lock<std::mutex>(stripes_[stripe(identity)]);
}
}
