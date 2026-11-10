#include "../tools/PackageSignals.h"
#include <chrono>
#include <csignal>
#include <future>
#include <stop_token>
#include <stdexcept>
#include <unistd.h>

using namespace std::chrono_literals;
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    volatile std::sig_atomic_t interrupted = 0;
    for (const auto signal : {SIGINT, SIGTERM}) {
        std::stop_source cancellation;
        std::promise<void> delivered;
        auto ready = delivered.get_future();
        std::stop_callback callback(cancellation.get_token(), [&] { delivered.set_value(); });
        interrupted = 0;
        {
            iiFileProvider::cli::PackageSignals signals(cancellation, interrupted);
            check(!cancellation.stop_requested(), "idle signal monitor cancelled work");
            check(::kill(::getpid(), signal) == 0, "failed to send the test signal");
            check(ready.wait_for(2s) == std::future_status::ready, "signal did not wake cancellation");
        }
        check(interrupted && cancellation.stop_requested(), "cancellation was not retained after shutdown");
    }
    std::stop_source idle;
    interrupted = 0;
    { iiFileProvider::cli::PackageSignals signals(idle, interrupted); }
    check(!interrupted && !idle.stop_requested(), "normal monitor shutdown requested cancellation");
}
