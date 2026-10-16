#pragma once
#include "iiFileProviderNativeExport.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
struct sqlite3;
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winternl.h>
#else
#include <sys/types.h>
#endif

namespace iiFileProvider::native {
// Streaming files preserve append/seek semantics (logs, journals, model caches).
// Complete document encoders should prefer StagedFile + publication instead.
class IIFILEPROVIDER_NATIVE_EXPORT OutputFile : public std::ofstream {
public:
    OutputFile() = default;
    explicit OutputFile(const std::filesystem::path &, std::ios::openmode = std::ios::out);
    ~OutputFile();
    void open(const std::filesystem::path &, std::ios::openmode = std::ios::out);
    void close();
};
class IIFILEPROVIDER_NATIVE_EXPORT ReadWriteFile : public std::fstream {
public:
    ReadWriteFile() = default;
    explicit ReadWriteFile(const std::filesystem::path &, std::ios::openmode = std::ios::in | std::ios::out);
    ~ReadWriteFile();
    void open(const std::filesystem::path &, std::ios::openmode = std::ios::in | std::ios::out);
    void close();
};
IIFILEPROVIDER_NATIVE_EXPORT FILE *openStream(const char *, const char *);
IIFILEPROVIDER_NATIVE_EXPORT int openDatabase(const char *, sqlite3 **, int flags, const char *vfs = nullptr);
IIFILEPROVIDER_NATIVE_EXPORT bool createDirectory(const std::filesystem::path &);
IIFILEPROVIDER_NATIVE_EXPORT bool createDirectory(const std::filesystem::path &, std::error_code &);
IIFILEPROVIDER_NATIVE_EXPORT bool createDirectories(const std::filesystem::path &);
IIFILEPROVIDER_NATIVE_EXPORT bool createDirectories(const std::filesystem::path &, std::error_code &);
IIFILEPROVIDER_NATIVE_EXPORT bool copyFile(const std::filesystem::path &, const std::filesystem::path &,
    std::filesystem::copy_options = std::filesystem::copy_options::none);
IIFILEPROVIDER_NATIVE_EXPORT bool copyFile(const std::filesystem::path &, const std::filesystem::path &,
    std::filesystem::copy_options, std::error_code &);
IIFILEPROVIDER_NATIVE_EXPORT void renameFile(const std::filesystem::path &, const std::filesystem::path &);
IIFILEPROVIDER_NATIVE_EXPORT void renameFile(const std::filesystem::path &, const std::filesystem::path &, std::error_code &);
IIFILEPROVIDER_NATIVE_EXPORT void createHardLink(const std::filesystem::path &, const std::filesystem::path &);
#ifndef _WIN32
// Descriptor-relative operations retain caller confinement, O_NOFOLLOW,
// O_EXCL and permissions. Descriptors remain caller-owned.
IIFILEPROVIDER_NATIVE_EXPORT int openFile(const char *, int, mode_t = 0666);
IIFILEPROVIDER_NATIVE_EXPORT int openFileAt(int, const char *, int, mode_t = 0666);
IIFILEPROVIDER_NATIVE_EXPORT int makeDirectory(const char *, mode_t);
IIFILEPROVIDER_NATIVE_EXPORT int makeDirectoryAt(int, const char *, mode_t);
#else
IIFILEPROVIDER_NATIVE_EXPORT HANDLE createFile(const wchar_t *, DWORD, DWORD,
    LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
IIFILEPROVIDER_NATIVE_EXPORT BOOL createDirectoryWindows(const wchar_t *, LPSECURITY_ATTRIBUTES);
IIFILEPROVIDER_NATIVE_EXPORT BOOL moveFile(const wchar_t *, const wchar_t *, DWORD);
IIFILEPROVIDER_NATIVE_EXPORT NTSTATUS createFileNt(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
    PIO_STATUS_BLOCK, PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
#endif
} // namespace iiFileProvider::native
