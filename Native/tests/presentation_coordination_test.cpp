#include "../src/Presentation/ChangeTokenCoordinator.h"
#include <chrono>
#include <future>
#include <stdexcept>
#include <string>

using namespace std::chrono_literals;
using iiFileProvider::native::detail::ChangeTokenCoordinator;

int main() {
    ChangeTokenCoordinator coordinator;
    const std::string first = "device:one";
    std::string second = "device:two";
    while (coordinator.stripe(first) == coordinator.stripe(second)) second += 'x';
    std::future<void> same, other;
    bool sameBlocked = false, otherReady = false;
    {
        auto publication = coordinator.lock(first);
        std::promise<void> started;
        auto ready = started.get_future();
        same = std::async(std::launch::async, [&] {
            started.set_value();
            auto reading = coordinator.lock(first);
        });
        ready.wait();
        sameBlocked = same.wait_for(100ms) == std::future_status::timeout;
        other = std::async(std::launch::async, [&] { auto reading = coordinator.lock(second); });
        otherReady = other.wait_for(2s) == std::future_status::ready;
    }
    same.get(); other.get();
    if (!sameBlocked || !otherReady)
        throw std::runtime_error("Presentation must serialize the same identity without blocking unrelated identities");
}
