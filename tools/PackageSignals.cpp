#include "PackageSignals.h"
#include <system_error>

namespace iiFileProvider::cli {
PackageSignals::PackageSignals(std::stop_source &cancellation,
                               volatile std::sig_atomic_t &interrupted)
    : cancellation_(cancellation), interrupted_(interrupted) {
    sigemptyset(&mask_);
    sigaddset(&mask_, SIGINT);
    sigaddset(&mask_, SIGTERM);
    if (const auto error = pthread_sigmask(SIG_BLOCK, &mask_, &previous_))
        throw std::system_error(error, std::generic_category(), "Cannot block package signals");
    try {
        waiter_ = std::thread([this] {
            for (;;) {
                int signal = 0;
                if (sigwait(&mask_, &signal) != 0) {
                    interrupted_ = 1;
                    cancellation_.request_stop();
                    return;
                }
                if (stopping_.load(std::memory_order_acquire)) return;
                if (signal == SIGINT || signal == SIGTERM) {
                    interrupted_ = 1;
                    cancellation_.request_stop();
                }
            }
        });
    } catch (...) {
        (void)pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
        throw;
    }
}
PackageSignals::~PackageSignals() {
    stopping_.store(true, std::memory_order_release);
    if (waiter_.joinable()) {
        (void)pthread_kill(waiter_.native_handle(), SIGTERM);
        waiter_.join();
    }
    (void)pthread_sigmask(SIG_SETMASK, &previous_, nullptr);
}
}
