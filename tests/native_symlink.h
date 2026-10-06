#pragma once
#include <filesystem>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace test_support {
inline void create_symlink(const std::filesystem::path& target,
                           const std::filesystem::path& link, bool directory = false) {
#ifdef _WIN32
    const DWORD kind = directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0;
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), kind | 0x2))
        return;
    if (GetLastError() == ERROR_INVALID_PARAMETER &&
        CreateSymbolicLinkW(link.c_str(), target.c_str(), kind))
        return;
    throw std::filesystem::filesystem_error("CreateSymbolicLinkW", target, link,
        std::error_code(GetLastError(), std::system_category()));
#else
    if (directory) std::filesystem::create_directory_symlink(target, link);
    else std::filesystem::create_symlink(target, link);
#endif
}
}
