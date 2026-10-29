#include "iiFileProviderCoreIO.h"
#include <iiFileProviderStorage.h>
#include <iiFileProviderPresentation.h>
#include <iiSDKCore/IO/NativeFile.h>
#include <cerrno>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace iiFileProvider::core {
namespace {
using namespace ii::io;
auto failure(ErrorCode code, std::error_code native = {}) { return std::unexpected(Error{code, native}); }
auto native_failure() {
#if defined(_WIN32)
    const auto number = GetLastError();
    auto code = ErrorCode::system_error;
    switch (number) {
    case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: code = ErrorCode::not_found; break;
    case ERROR_ACCESS_DENIED: case ERROR_SHARING_VIOLATION: code = ErrorCode::permission_denied; break;
    case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: code = ErrorCode::already_exists; break;
    case ERROR_INVALID_PARAMETER: code = ErrorCode::invalid_argument; break;
    }
    return failure(code, {static_cast<int>(number), std::system_category()});
#else
    const auto number = errno;
    auto code = ErrorCode::system_error;
    switch (number) {
    case ENOENT: case ENOTDIR: code = ErrorCode::not_found; break;
    case EACCES: case EPERM: code = ErrorCode::permission_denied; break;
    case EEXIST: code = ErrorCode::already_exists; break;
    case EINVAL: case EOVERFLOW: code = ErrorCode::invalid_argument; break;
    }
    return failure(code, {number, std::generic_category()});
#endif
}
struct Handle final {
#if defined(_WIN32)
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    std::intptr_t release() { const auto old = value; value = INVALID_HANDLE_VALUE; return reinterpret_cast<std::intptr_t>(old); }
#else
    int value = -1;
    ~Handle() { if (value >= 0) ::close(value); }
    std::intptr_t release() { const auto old = value; value = -1; return old; }
#endif
};
}
Result<ii::io::Channel> open_file(const std::filesystem::path &path, ii::io::FileOptions options) {
    using namespace ii::io;
    if (path.empty() || path.native().find(std::filesystem::path::value_type{}) != std::filesystem::path::string_type::npos
        || (options.access != Access::read && options.access != Access::write && options.access != Access::read_write)
        || (options.creation != Creation::existing && options.creation != Creation::open_or_create
            && options.creation != Creation::create_new && options.creation != Creation::truncate)
        || (options.access == Access::read && options.creation != Creation::existing))
        return failure(ErrorCode::invalid_argument);
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error).lexically_normal();
    if (error) return failure(ErrorCode::system_error, error);
    auto notification = options.access == Access::read ? std::filesystem::path{} : absolute;
    Handle handle;
#if defined(_WIN32)
    const auto mode = options.creation == Creation::create_new ? CREATE_NEW
        : options.creation == Creation::existing ? OPEN_EXISTING : OPEN_ALWAYS;
    const DWORD access = (options.access != Access::write ? GENERIC_READ : 0)
        | (options.access != Access::read ? GENERIC_WRITE : 0);
    handle.value = native::createFile(absolute.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, mode, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle.value == INVALID_HANDLE_VALUE) return native_failure();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(handle.value, &info)) return native_failure();
    if (GetFileType(handle.value) != FILE_TYPE_DISK || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return failure(ErrorCode::unsupported);
    if (options.creation == Creation::truncate && !SetEndOfFile(handle.value)) return native_failure();
#else
    int flags = options.access == Access::read ? O_RDONLY : options.access == Access::write ? O_WRONLY : O_RDWR;
    if (options.creation != Creation::existing) flags |= O_CREAT;
    if (options.creation == Creation::create_new) flags |= O_EXCL;
    do { handle.value = native::openFile(absolute.c_str(), flags | O_CLOEXEC | O_NONBLOCK, 0600); }
    while (handle.value < 0 && errno == EINTR);
    if (handle.value < 0) return native_failure();
    struct stat info{};
    if (fstat(handle.value, &info) != 0) return native_failure();
    if (!S_ISREG(info.st_mode)) return failure(ErrorCode::unsupported);
    if (options.creation == Creation::truncate && ftruncate(handle.value, 0) != 0) return native_failure();
#endif
    return NativeFile::adopt(handle.release(), options.access, std::move(notification),
        options.access == Access::read ? nullptr : &native::Presentation::enqueue);
}
} // namespace iiFileProvider::core
