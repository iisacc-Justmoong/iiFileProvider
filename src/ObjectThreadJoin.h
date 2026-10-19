#pragma once
#include <thread>
#include <vector>

namespace iiFileProvider::detail {
// Batch workers consume the caller's stop_token; their only implicit jthread
// behavior was joining. Keep that lifetime guarantee with Emscripten libc++.
class ObjectThreadJoin final {
public:
    explicit ObjectThreadJoin(std::vector<std::thread> &threads) : threads_(threads) {}
    ~ObjectThreadJoin() { join(); }
    ObjectThreadJoin(const ObjectThreadJoin &) = delete;
    ObjectThreadJoin &operator=(const ObjectThreadJoin &) = delete;
    void join() { for(auto &thread : threads_) if(thread.joinable()) thread.join(); }
private:
    std::vector<std::thread> &threads_;
};
}
