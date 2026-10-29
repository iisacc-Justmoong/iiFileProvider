#include "TemporaryDirectory.h"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <unistd.h>
#endif
namespace fs = std::filesystem;
int main() {
#ifndef _WIN32
    const auto root = fs::current_path() / "temporary-anchor-contract";
    const auto restricted = root / "restricted";
    const auto anchor = restricted / "container";
    try {
        fs::create_directories(anchor / "nested");
        fs::create_directory_symlink(anchor / "nested", anchor / "redirect");
        fs::permissions(restricted, fs::perms::owner_exec);
        int descriptor = iiFileProvider::detail::open_lease_directory(anchor / "nested", anchor);
        if (descriptor < 0) throw std::runtime_error("Container access failed");
        ::close(descriptor);
        const auto rejects = [&](const fs::path& path) {
            try { auto fd=iiFileProvider::detail::open_lease_directory(path, anchor); ::close(fd); }
            catch (const std::exception&) { return; }
            throw std::runtime_error("Redirect or container escape was accepted");
        };
        rejects(anchor / "redirect");
        rejects(root);
        rejects(anchor / ".." / "container");
        bool root_walk_rejected = false;
        try { auto fd=iiFileProvider::detail::open_lease_directory(anchor, "/"); ::close(fd); }
        catch (const std::exception&) { root_walk_rejected = true; }
        if (!root_walk_rejected) throw std::runtime_error("Fixture did not restrict ancestor reads");
        fs::permissions(restricted, fs::perms::owner_all);
        fs::remove_all(root);
        std::cout << "TEMPORARY_ANCHOR_OK container=true ancestors_unreadable=true symlink_rejected=true escape_rejected=true\n";
    } catch (const std::exception& error) {
        std::error_code ignored;
        fs::permissions(restricted, fs::perms::owner_all, ignored);
        fs::remove_all(root, ignored);
        std::cerr << error.what() << '\n';
        return 1;
    }
#endif
}
