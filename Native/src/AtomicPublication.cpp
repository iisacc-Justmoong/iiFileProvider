#include "AtomicPublication.h"
#include "iiFileProviderNative.h"

#include <cerrno>
#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <stdio.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace iiFileProvider::native::atomic_detail {
std::filesystem::path checked(const std::filesystem::path &path)
{
    if (path.empty() || path.filename().empty()
        || path.native().find(std::filesystem::path::value_type{}) != path.native().npos)
        throw FileError("A local filename without NUL is required", FileCode::InvalidPath);
    return std::filesystem::absolute(path).lexically_normal();
}

std::error_code moveFile(const std::filesystem::path &source,
                         const std::filesystem::path &destination, bool overwrite)
{
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(),
                     MOVEFILE_WRITE_THROUGH | (overwrite ? MOVEFILE_REPLACE_EXISTING : 0)))
        return {int(GetLastError()), std::system_category()};
#else
    if (overwrite) {
        if (::rename(source.c_str(), destination.c_str())) return {errno, std::generic_category()};
    }
#if defined(__APPLE__)
    else if (renamex_np(source.c_str(), destination.c_str(), RENAME_EXCL)) return {errno, std::generic_category()};
#elif defined(__linux__) && defined(SYS_renameat2)
    else if (syscall(SYS_renameat2, AT_FDCWD, source.c_str(), AT_FDCWD, destination.c_str(), 1))
        return {errno, std::generic_category()};
#else
    else if (::link(source.c_str(), destination.c_str())) return {errno, std::generic_category()};
    else if (::unlink(source.c_str())) return {errno, std::generic_category()};
#endif
#endif
    return {};
}

std::error_code moveDirectoryExclusive(const std::filesystem::path &source,
                                       const std::filesystem::path &destination)
{
#if defined(_WIN32) || defined(__APPLE__) || (defined(__linux__) && defined(SYS_renameat2))
    return moveFile(source, destination, false);
#else
    (void)source; (void)destination;
    return std::make_error_code(std::errc::operation_not_supported);
#endif
}
}
