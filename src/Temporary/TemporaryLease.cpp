#include "TemporaryLease.h"
#include "TemporaryDirectory.h"
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#endif

namespace iiFileProvider {
namespace {
namespace fs = std::filesystem;
constexpr auto sentinel = ".iisacc-temporary.lock";
[[noreturn]] void fail(const char *message) { throw std::runtime_error(message); }
[[noreturn]] void io(const char *message, int code) {
    throw std::system_error(code, std::system_category(), message);
}
void validate(const fs::path &directory, TemporaryLease::Mode mode) {
    if (mode != TemporaryLease::Mode::Shared && mode != TemporaryLease::Mode::TryExclusive)
        fail("invalid temporary lease mode");
    if (directory.empty() || !directory.is_absolute() || directory.lexically_normal() != directory)
        fail("temporary lease requires an absolute normalized directory");
    const auto &native = directory.native();
    if (native.find(fs::path::value_type{}) != fs::path::string_type::npos)
        fail("temporary lease path contains a null character");
}
#ifndef _WIN32
struct Descriptor {
    int value = -1;
    ~Descriptor() { if (value >= 0) ::close(value); }
    void reset(int next) { if (value >= 0) ::close(value); value = next; }
};
bool beneath(const fs::path& directory, const fs::path& root) {
    if (directory == root) return true;
    const auto relative = directory.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& component : relative) if (component == "..") return false;
    return true;
}
int openDirectory(const fs::path &directory) {
#if defined(__APPLE__) && TARGET_OS_IOS
    // iPadOS permits access to the app container, but opening protected system
    // ancestors for directory reading is forbidden. HOME is the OS-assigned
    // container boundary; descendants retain the no-follow contract.
    if (const auto* home = std::getenv("HOME")) {
        const auto root = fs::path(home).lexically_normal();
        if (root.is_absolute() && beneath(directory, root))
            return detail::open_lease_directory(directory, root);
        std::error_code error;
        const auto canonical = fs::canonical(root, error);
        if (!error && beneath(directory, canonical))
            return detail::open_lease_directory(directory, canonical);
    }
#endif
    return detail::open_lease_directory(directory, "/");
}
int openRelative(const fs::path& directory, const fs::path& root) {
    if (!root.is_absolute() || root.lexically_normal() != root ||
        !directory.is_absolute() || directory.lexically_normal() != directory ||
        !beneath(directory, root)) fail("temporary lease path escapes its trusted root");
    Descriptor parent;
    parent.value = ::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (parent.value < 0) io("cannot open temporary lease root", errno);
    for (const auto &part : directory.lexically_relative(root)) {
        if (part.empty() || part == ".") continue;
        const auto next = ::openat(parent.value, part.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) io("temporary lease directory is unavailable or redirected", errno);
        parent.reset(next);
    }
    const auto result = parent.value; parent.value = -1; return result;
}
void inspect(int directory, int lock) {
    struct stat handle{}, entry{};
    if (::fstat(lock, &handle) || ::fstatat(directory, sentinel, &entry, AT_SYMLINK_NOFOLLOW))
        io("cannot inspect temporary lease sentinel", errno);
    if (!S_ISREG(handle.st_mode) || !S_ISREG(entry.st_mode) || handle.st_size != 0
        || handle.st_nlink != 1 || handle.st_dev != entry.st_dev || handle.st_ino != entry.st_ino)
        fail("temporary lease sentinel is redirected or not an empty regular file");
}
#else
void directDirectory(const fs::path &directory) {
    auto prefix = directory.root_path();
    auto inspect = [](const fs::path &path) {
        const auto attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) io("temporary lease directory is unavailable", GetLastError());
        if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            fail("temporary lease directory is redirected or not a directory");
    };
    inspect(prefix);
    for (const auto &part : directory.relative_path()) {
        if (part.empty()) continue;
        prefix /= part; inspect(prefix);
    }
}
#endif
}
#ifndef _WIN32
int detail::open_lease_directory(const fs::path& directory, const fs::path& trusted_root) {
    return openRelative(directory, trusted_root);
}
#endif
class TemporaryLease::Impl {
public:
    bool acquired = false;
#ifdef _WIN32
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE file = INVALID_HANDLE_VALUE;
    OVERLAPPED overlap{};
    ~Impl() {
        if (file != INVALID_HANDLE_VALUE) {
            if (acquired) UnlockFileEx(file, 0, 1, 0, &overlap);
            CloseHandle(file);
        }
        if (directory != INVALID_HANDLE_VALUE) CloseHandle(directory);
    }
#else
    int file = -1;
    ~Impl() { if (file >= 0) { if (acquired) ::flock(file, LOCK_UN); ::close(file); } }
#endif
};
TemporaryLease::TemporaryLease(const fs::path &directory, Mode mode) : m_impl(std::make_unique<Impl>()) {
    validate(directory, mode);
#ifdef _WIN32
    directDirectory(directory);
    // Without FILE_SHARE_DELETE the pinned parent/sentinel cannot be renamed
    // or deleted underneath a participating writer or cleaner.
    m_impl->directory = CreateFileW(directory.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (m_impl->directory == INVALID_HANDLE_VALUE) io("cannot pin temporary lease directory", GetLastError());
    BY_HANDLE_FILE_INFORMATION parent{};
    if (!GetFileInformationByHandle(m_impl->directory, &parent)) io("cannot inspect temporary lease directory", GetLastError());
    if (!(parent.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || parent.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        fail("temporary lease parent is redirected");
    m_impl->file = CreateFileW((directory / sentinel).c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (m_impl->file == INVALID_HANDLE_VALUE) io("cannot open temporary lease sentinel", GetLastError());
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(m_impl->file, &info)) io("cannot inspect temporary lease sentinel", GetLastError());
    if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)
        || info.nFileSizeHigh || info.nFileSizeLow || info.nNumberOfLinks != 1)
        fail("temporary lease sentinel is redirected or not an empty regular file");
    const DWORD flags = mode == Mode::Shared ? 0 : LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY;
    if (!LockFileEx(m_impl->file, flags, 0, 1, 0, &m_impl->overlap)) {
        const auto code = GetLastError();
        if (mode == Mode::TryExclusive && code == ERROR_LOCK_VIOLATION) return;
        io("cannot acquire temporary lease", code);
    }
    m_impl->acquired = true;
#else
    Descriptor parent; parent.value = openDirectory(directory);
    int openError = 0;
    for (unsigned attempt = 0; attempt != 8; ++attempt) {
        m_impl->file = ::openat(parent.value, sentinel, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        if (m_impl->file >= 0) break;
        openError = errno;
        if (openError == EINTR) continue;
        if (openError != ENOENT) break;
        // Concurrent first creation can transiently report ENOENT on APFS.
        // Retry only while the no-follow parent is still the same live folder;
        // a removed, replaced or redirected parent must remain an error.
        struct stat pinned{}, current{};
        if (::fstat(parent.value, &pinned) || ::lstat(directory.c_str(), &current)
            || !S_ISDIR(pinned.st_mode) || !S_ISDIR(current.st_mode) || !pinned.st_nlink
            || pinned.st_dev != current.st_dev || pinned.st_ino != current.st_ino) break;
        const timespec pause{0, 1000000}; ::nanosleep(&pause, nullptr);
    }
    if (m_impl->file < 0) io("cannot open temporary lease sentinel", openError);
    inspect(parent.value, m_impl->file);
    const auto operation = mode == Mode::Shared ? LOCK_SH : LOCK_EX | LOCK_NB;
    while (::flock(m_impl->file, operation) != 0) {
        if (errno == EINTR) continue;
        if (mode == Mode::TryExclusive && (errno == EWOULDBLOCK || errno == EAGAIN)) return;
        io("cannot acquire temporary lease", errno);
    }
    m_impl->acquired = true;
    inspect(parent.value, m_impl->file);
#endif
}
TemporaryLease::~TemporaryLease() = default;
TemporaryLease::TemporaryLease(TemporaryLease &&) noexcept = default;
TemporaryLease &TemporaryLease::operator=(TemporaryLease &&) noexcept = default;
bool TemporaryLease::locked() const noexcept { return m_impl && m_impl->acquired; }
}
