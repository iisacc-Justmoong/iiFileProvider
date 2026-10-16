#include "TemporaryLease.h"
#include "native_symlink.h"
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
using iiFileProvider::TemporaryLease;
using Mode = TemporaryLease::Mode;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    bool rejected = false;
    try { operation(); } catch (const std::exception &) { rejected = true; }
    check(rejected, "invalid temporary lease must reject");
}
int child(const fs::path &executable, const fs::path &directory) {
#ifdef _WIN32
    const wchar_t *arguments[] = {executable.c_str(), L"--probe", directory.c_str(), nullptr};
    const auto result = _wspawnv(_P_WAIT, executable.c_str(), arguments);
    check(result >= 0, "spawn temporary lease child"); return static_cast<int>(result);
#else
    const auto process = ::fork();
    check(process >= 0, "fork temporary lease child");
    if (process == 0) {
        ::execl(executable.c_str(), executable.c_str(), "--probe", directory.c_str(), static_cast<char *>(nullptr));
        ::_exit(2);
    }
    int status{}; check(::waitpid(process, &status, 0) == process && WIFEXITED(status), "temporary lease child exit");
    return WEXITSTATUS(status);
#endif
}
int main(int argc, char **argv) { try {
    if (argc == 3 && std::string(argv[1]) == "--probe") {
        TemporaryLease probe(fs::path(argv[2]), Mode::TryExclusive);
        return probe.locked() ? 0 : 3;
    }
    static_assert(!std::is_copy_constructible_v<TemporaryLease>);
    static_assert(std::is_nothrow_move_constructible_v<TemporaryLease>);
    const auto root = fs::canonical(fs::current_path()) / ("temporary-lease-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);
    for (unsigned round = 0; round != 32; ++round) {
        const auto directory = root / ("concurrent-" + std::to_string(round)); fs::create_directory(directory);
        std::barrier start(8); std::atomic_uint failures = 0; std::vector<std::jthread> writers;
        for (unsigned n = 0; n != 8; ++n) writers.emplace_back([&] {
            start.arrive_and_wait();
            try { TemporaryLease lease(directory, Mode::Shared); if (!lease.locked()) ++failures; }
            catch (const std::exception &) { ++failures; }
        });
        writers.clear();
        check(failures == 0, "concurrent first sentinel creation must admit every shared writer");
        check(fs::file_size(directory / ".iisacc-temporary.lock") == 0, "concurrent sentinel remains empty");
    }
    const auto executable = fs::canonical(fs::absolute(argv[0]));
    {
        TemporaryLease writer(root, Mode::Shared);
        TemporaryLease other(root, Mode::Shared);
        check(writer.locked() && other.locked(), "writers share one directory lease");
        TemporaryLease cleaner(root, Mode::TryExclusive);
        check(!cleaner.locked(), "cleaner skips active writers in the same process");
        check(child(executable, root) == 3, "cleaner in another process skips an active writer");
        TemporaryLease moved(std::move(writer));
        check(moved.locked() && !writer.locked(), "move transfers active lease ownership");
    }
    check(child(executable, root) == 0, "another process acquires after writers release");
    {
        auto cleaner = std::make_unique<TemporaryLease>(root, Mode::TryExclusive);
        check(cleaner->locked(), "cleaner acquires after every writer releases");
        TemporaryLease competitor(root, Mode::TryExclusive);
        check(!competitor.locked(), "only one cleaner can acquire");
        std::promise<void> entered;
        auto writer = std::async(std::launch::async, [&] {
            entered.set_value();
            TemporaryLease lease(root, Mode::Shared); return lease.locked();
        });
        entered.get_future().wait();
        check(writer.wait_for(std::chrono::milliseconds(50)) == std::future_status::timeout,
            "writer waits for cleanup before creating its temporary");
        cleaner.reset();
        check(writer.wait_for(std::chrono::seconds(3)) == std::future_status::ready && writer.get(),
            "writer proceeds when cleanup finishes");
    }
    check(fs::file_size(root / ".iisacc-temporary.lock") == 0, "persistent sentinel allocates no file payload");
    rejects([&] { TemporaryLease invalid(root / "missing", Mode::Shared); });
    rejects([&] { TemporaryLease invalid("relative", Mode::Shared); });
    rejects([&] { TemporaryLease invalid(root / ".." / root.filename(), Mode::Shared); });
    fs::create_directory(root / "target");
    test_support::create_symlink(root / "target", root / "redirect", true);
    rejects([&] { TemporaryLease invalid(root / "redirect", Mode::Shared); });
    fs::create_directory(root / "linked-lock");
    std::ofstream(root / "unrelated") << "preserve";
    test_support::create_symlink(root / "unrelated", root / "linked-lock/.iisacc-temporary.lock");
    rejects([&] { TemporaryLease invalid(root / "linked-lock", Mode::TryExclusive); });
    fs::create_directory(root / "invalid-lock");
    std::ofstream(root / "invalid-lock/.iisacc-temporary.lock") << "preserve";
    rejects([&] { TemporaryLease invalid(root / "invalid-lock", Mode::Shared); });
    check(fs::file_size(root / "unrelated") == 8 && fs::file_size(root / "invalid-lock/.iisacc-temporary.lock") == 8,
        "refusing invalid sentinels preserves existing files");
    fs::remove_all(root);
    std::cout << "Shared writers, blocking admission, cross-process cleanup exclusion, lifetime and direct paths passed\n";
    return 0;
} catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; } }
