#include "File.h"
#include <QFileInfo>
#include <QLockFile>

#include <cerrno>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <stdio.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#endif
#endif

namespace iiFileProvider {

#ifndef _WIN32
namespace {

std::error_code posixError(int errorNumber)
{
    return {errorNumber, std::generic_category()};
}

AtomicFileCommitResult applyOrdinaryNewFilePermissions(
    const std::filesystem::path& temporary)
{
    auto probe = temporary;
    probe += ".permission-probe";

    int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_CLOEXEC
    flags |= O_CLOEXEC;
#endif
    const int descriptor = ::open(probe.c_str(), flags, 0666);
    if (descriptor < 0) {
        const auto error = posixError(errno);
        return {
            false,
            "destination_permission_probe_failed",
            "The new-destination permission probe could not be created: "
                + error.message()};
    }

    struct stat status {};
    const int statusResult = ::fstat(descriptor, &status);
    const int statusError = statusResult == 0 ? 0 : errno;

    const int unlinkResult = ::unlink(probe.c_str());
    const int unlinkError = unlinkResult == 0 ? 0 : errno;
    const int closeResult = ::close(descriptor);
    const int closeError = closeResult == 0 ? 0 : errno;

    if (statusError != 0) {
        return {
            false,
            "destination_permission_probe_status_failed",
            "The new-destination permission probe mode could not be read: "
                + posixError(statusError).message()};
    }
    if (unlinkError != 0) {
        return {
            false,
            "destination_permission_probe_cleanup_failed",
            "The new-destination permission probe could not be removed: "
                + posixError(unlinkError).message()};
    }
    if (closeError != 0) {
        return {
            false,
            "destination_permission_probe_close_failed",
            "The new-destination permission probe could not be closed: "
                + posixError(closeError).message()};
    }

    const mode_t permissions = status.st_mode
        & static_cast<mode_t>(S_IRWXU | S_IRWXG | S_IRWXO);
    if (::chmod(temporary.c_str(), permissions) != 0) {
        const auto error = posixError(errno);
        return {
            false,
            "temporary_permissions_failed",
            "The ordinary new-file permissions could not be applied to the replacement: "
                + error.message()};
    }
    return {true, {}, {}};
}

} // namespace
#endif

AtomicFileCommitResult File::publish(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination, bool overwrite)
{
    QString target, sourcePath;
    try {
        target = absolutePath(pathString(destination));
        sourcePath = absolutePath(pathString(temporary));
    }
    catch (const std::exception &error) { return {false, "invalid_path", error.what()}; }
    QLockFile lock(target + ".iisacc-lock");
    if (!lock.tryLock(0)) return {false, "file_busy", "another file operation owns this path"};
    const QFileInfo info(target);
    const QFileInfo source(sourcePath);
    if (source.isSymLink() || !source.isFile() || source.absoluteFilePath() == info.absoluteFilePath())
        return {false, "invalid_source", "publication requires a distinct regular temporary file"};
    if (info.isSymLink() || (info.exists() && !info.isFile()))
        return {false, "invalid_path", "destination must be a regular file"};
    if (!overwrite && info.exists()) return {false, "already_exists", "destination already exists"};
    std::error_code error;
    const bool destinationExists = std::filesystem::exists(destination, error);
    if (error) {
        return {
            false,
            "destination_status_failed",
            "The existing destination could not be inspected: " + error.message()};
    }
    if (destinationExists) {
        const auto destinationPermissions =
            std::filesystem::status(destination, error).permissions();
        if (error) {
            return {
                false,
                "destination_permissions_failed",
                "The existing destination permissions could not be read: "
                    + error.message()};
        }
        std::filesystem::permissions(
            temporary,
            destinationPermissions,
            std::filesystem::perm_options::replace,
            error);
        if (error) {
            return {
                false,
                "temporary_permissions_failed",
                "The replacement permissions could not be preserved: "
                    + error.message()};
        }
    }
#ifndef _WIN32
    else {
        const auto permissions = applyOrdinaryNewFilePermissions(temporary);
        if (!permissions.succeeded) {
            return permissions;
        }
    }
#endif

#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(), destination.c_str(),
            (overwrite ? MOVEFILE_REPLACE_EXISTING : 0) | MOVEFILE_WRITE_THROUGH)) {
        error = std::error_code(
            static_cast<int>(GetLastError()), std::system_category());
    }
#else
    if (overwrite) {
        std::filesystem::rename(temporary, destination, error);
    }
#if defined(__APPLE__)
    else if (renamex_np(temporary.c_str(), destination.c_str(), RENAME_EXCL) != 0) {
        error = posixError(errno);
    }
#elif defined(__linux__) && defined(SYS_renameat2)
    else if (syscall(SYS_renameat2, AT_FDCWD, temporary.c_str(), AT_FDCWD, destination.c_str(), 1) != 0) {
        error = posixError(errno);
    }
#else
    else if (::link(temporary.c_str(), destination.c_str()) != 0) {
        error = posixError(errno);
    } else {
        // Publication succeeded. A temporary-file owner cleans up if unlink fails.
        (void)::unlink(temporary.c_str());
    }
#endif
#endif
    if (error) {
        if (!overwrite && error == std::errc::file_exists)
            return {false, "already_exists", "destination already exists"};
        return {
            false,
            "atomic_commit_failed",
            "The validated temporary file could not replace its destination atomically: "
                + error.message()};
    }
    return {true, {}, {}};
}

} // namespace iiFileProvider
