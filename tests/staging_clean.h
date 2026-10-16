#pragma once
#include <filesystem>
namespace test_support {
inline bool staging_clean(const std::filesystem::path &directory) {
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().filename() != ".iisacc-temporary.lock"
            || !std::filesystem::is_regular_file(entry.symlink_status()) || entry.file_size() != 0)
            return false;
    }
    return true;
}
}
