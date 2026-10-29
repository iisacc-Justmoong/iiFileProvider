#pragma once
#include <filesystem>

namespace iiFileProvider::detail {
#ifndef _WIN32
// Internal POSIX boundary. The caller transfers a trusted, existing root;
// every descendant is opened relative to pinned descriptors without symlinks.
// The returned descriptor belongs to the caller.
int open_lease_directory(const std::filesystem::path& directory,
                         const std::filesystem::path& trusted_root);
#endif
}
