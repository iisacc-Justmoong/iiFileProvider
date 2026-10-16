#pragma once
#include <ObjectSource.h>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <vector>

namespace test_support {
// macOS can asynchronously attach provenance to newly generated fixtures,
// changing ctime without a test edit. Establish a quiet fixture before taking
// the inventory being asserted; production change detection remains strict.
inline void settle_files(const std::vector<std::filesystem::path> &paths) {
#ifdef __APPLE__
    using namespace std::chrono;
    // On this host the first native clone, rather than a plain read, triggers
    // provenance. Prime synthetic fixtures without changing source attributes
    // ourselves, then wait for the observed OS update before asserting identity.
    const auto staging = std::filesystem::current_path() / ("fixture-prime-" + std::to_string(steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(staging);
    try {
        for (const auto &path : paths) { iiFileProvider::ObjectSource snapshot(path, staging); }
    } catch (...) { std::filesystem::remove_all(staging); throw; }
    std::filesystem::remove_all(staging);
    const auto stamps = [&] {
        std::vector<std::string> result;
        for (const auto &path : paths) result.push_back(iiFileProvider::ObjectSource::inspect(path).stamp);
        return result;
    };
    auto previous = stamps(); auto quiet = steady_clock::now();
    const auto deadline = quiet + seconds(8);
    while (steady_clock::now() - quiet < seconds(1)) {
        if (steady_clock::now() > deadline) throw std::runtime_error("fixture metadata did not settle");
        std::this_thread::sleep_for(milliseconds(100));
        auto current = stamps();
        if (current != previous) { previous = std::move(current); quiet = steady_clock::now(); }
    }
#endif
}
inline void settle_tree(const std::filesystem::path &root) {
    std::vector<std::filesystem::path> files;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
        if (std::filesystem::is_regular_file(entry.symlink_status())) files.push_back(entry.path());
    settle_files(files);
}
} // namespace test_support
