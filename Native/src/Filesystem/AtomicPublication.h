#pragma once

#include <filesystem>
#include <system_error>

namespace iiFileProvider::native::atomic_detail {
std::filesystem::path checked(const std::filesystem::path &path);
std::error_code moveFile(const std::filesystem::path &source,
                         const std::filesystem::path &destination, bool overwrite);
std::error_code moveDirectoryExclusive(const std::filesystem::path &source,
                                       const std::filesystem::path &destination);
}
