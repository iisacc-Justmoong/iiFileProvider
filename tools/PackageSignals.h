#pragma once
#include <atomic>
#include <csignal>
#include <pthread.h>
#include <stop_token>
#include <thread>

namespace iiFileProvider::cli {
// Construct on the process's initial thread before starting any workers.
// New threads inherit the blocked termination signals; this object waits for
// them without a periodic observer and restores the original mask on teardown.
class PackageSignals final {
public:
    PackageSignals(std::stop_source &, volatile std::sig_atomic_t &);
    ~PackageSignals();
    PackageSignals(const PackageSignals &) = delete;
    PackageSignals &operator=(const PackageSignals &) = delete;
private:
    std::stop_source &cancellation_;
    volatile std::sig_atomic_t &interrupted_;
    sigset_t mask_{}, previous_{};
    std::atomic_bool stopping_{false};
    std::thread waiter_;
};
}
