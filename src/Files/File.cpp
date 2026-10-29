#include "File.h"
#include "StagedFile.h"
#include "Presentation_p.h"
#include <iiFileProviderNative.h>
#include "iiFileProviderDevice.h"
#include <QLockFile>
#include <algorithm>
#include <cerrno>
#include <mutex>
#if defined(__APPLE__)
#include <stdio.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace iiFileProvider {
QByteArray File::imageFormat(const QString &path) {
    const auto name=path.sliced(std::max(path.lastIndexOf('/'),path.lastIndexOf('\\'))+1);
    const auto dot=name.lastIndexOf('.');return dot<=0?QByteArray{}:name.sliced(dot+1).toLatin1();
}
FileError::FileError(FileCode code, const std::string &message)
    : std::runtime_error(message), m_code(code) {}
FileCode FileError::code() const noexcept { return m_code; }
namespace {
[[noreturn]] void fail(FileCode code, const QString &message) {
    throw FileError(code, message.toStdString());
}
class UnpublishedDevice final : public FileDevice {
public:
    using FileDevice::FileDevice;
    bool open(OpenMode mode) override {
        const bool result=FileDevice::open(mode);setPresentationEnabled(false);return result;
    }
};
std::unique_ptr<FileDevice> openSource(const QString &path) {
    const auto absolute = File::absolutePath(path);
    std::error_code error;
    const auto info=std::filesystem::status(std::filesystem::path(absolute.toStdU16String()),error);
    if (error==std::errc::no_such_file_or_directory) fail(FileCode::NotFound, "file does not exist");
    if (error) fail(FileCode::IoError, QString::fromStdString(error.message()));
    if (!std::filesystem::exists(info)) fail(FileCode::NotFound, "file does not exist");
    if (!std::filesystem::is_regular_file(info)) fail(FileCode::InvalidPath, "input must be a regular file");
    try {
        auto file = std::make_unique<FileDevice>(absolute);
        if (!file->open(QIODevice::ReadOnly)) fail(FileCode::IoError, file->errorString());
        return file;
    } catch (const native::FileError &error) { fail(FileCode::IoError, QString::fromUtf8(error.what())); }
}
QString destination(const QString &path) {
    const auto absolute = File::absolutePath(path);
    std::error_code error;
    const auto info=std::filesystem::symlink_status(std::filesystem::path(absolute.toStdU16String()),error);
    if (error && error!=std::errc::no_such_file_or_directory) fail(FileCode::IoError,QString::fromStdString(error.message()));
    if (std::filesystem::is_symlink(info) || (std::filesystem::exists(info) && !std::filesystem::is_regular_file(info)))
        fail(FileCode::InvalidPath, "destination must be a regular file, not a link or directory");
    return absolute;
}
void lock(QLockFile &file) {
    if (!file.tryLock(0)) fail(file.error() == QLockFile::LockFailedError ? FileCode::Conflict : FileCode::IoError,
                              "cannot acquire ownership of this path");
}
void writeUnlocked(const QString &path, const std::function<void(QIODevice &)> &encode) {
    if (!encode) fail(FileCode::InvalidPath, "a file encoder is required");
#ifdef _WIN32
    detail::cancelPresentation(std::filesystem::path(path.toStdU16String()));
#endif
    SaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) fail(FileCode::IoError, file.errorString());
    encode(file);
    if (!file.commit()) fail(FileCode::IoError, file.errorString());
    detail::presentLater(std::filesystem::path(path.toStdU16String()));
}
void transfer(QIODevice &input, QIODevice &output) {
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    for (;;) {
        const auto count = input.read(buffer.data(), buffer.size());
        if (count < 0) fail(FileCode::IoError, input.errorString());
        if (!count) {
            if (input.atEnd()) return;
            fail(FileCode::IoError, input.errorString());
        }
        if (output.write(buffer.constData(), count) != count) fail(FileCode::IoError, output.errorString());
    }
}
}
QString File::pathString(const std::filesystem::path &path) {
#ifdef _WIN32
    return QString::fromStdWString(path.native());
#else
    const auto &native = path.native();
    const auto value = QString::fromUtf8(native.data(), static_cast<qsizetype>(native.size()));
    if (value.toUtf8().toStdString() != native) fail(FileCode::InvalidPath, "path must be valid UTF-8");
    return value;
#endif
}
QString File::absolutePath(const QString &path) {
    if (path.isEmpty() || path.contains(QChar(u'\0')) || path == ":memory:"
        || path.startsWith(":") || path.contains("://"))
        fail(FileCode::InvalidPath, "a local filesystem path is required");
    if (QString::fromUtf8(path.toUtf8()) != path) fail(FileCode::InvalidPath, "invalid Unicode path");
    return pathString(std::filesystem::absolute(std::filesystem::path(path.toStdU16String())).lexically_normal());
}
std::unique_ptr<QIODevice> File::openRead(const QString &path) {
    return openSource(path);
}
QByteArray File::read(const QString &path, qint64 maximumBytes) {
    if (maximumBytes < 0 || maximumBytes >= std::numeric_limits<qsizetype>::max())
        fail(FileCode::LimitExceeded, "invalid file read limit");
    auto file = openSource(path);
    native::SourceAccess sourceAccess(std::filesystem::path(file->fileName().toStdU16String()));
    std::lock_guard access(sourceAccess);
    if (file->size() > maximumBytes) fail(FileCode::LimitExceeded, "file exceeds read limit");
    QByteArray bytes;
    while (!file->atEnd()) {
        const auto block = file->read(std::min<qint64>(1024 * 1024, maximumBytes - bytes.size() + 1));
        if (block.isEmpty()) fail(FileCode::IoError, file->errorString());
        if (block.size() > maximumBytes - bytes.size()) fail(FileCode::LimitExceeded, "file exceeds read limit");
        bytes += block;
    }
    return bytes;
}
QByteArray File::readPrefix(const QString &path, qint64 count) {
    if (count < 0) fail(FileCode::LimitExceeded, "negative prefix size");
    auto file = openSource(path);
    native::SourceAccess sourceAccess(std::filesystem::path(file->fileName().toStdU16String()));
    std::lock_guard access(sourceAccess);
    const auto bytes = file->read(count);
    if (bytes.size() < count && !file->atEnd()) fail(FileCode::IoError, file->errorString());
    return bytes;
}
bool File::prepareDatabase(const QString &path) {
    try {
        const auto target = destination(path);
        if (!FileDevice::exists(target)) {
            try { create(target, {}); }
            catch (const FileError &error) { if (error.code() != FileCode::AlreadyExists) throw; }
        }
        const auto info=std::filesystem::symlink_status(std::filesystem::path(target.toStdU16String()));
        return std::filesystem::is_regular_file(info);
    } catch (const FileError &) { return false; }
}
void File::writeWith(const QString &path, const std::function<void(QIODevice &)> &encode) {
    const auto target = destination(path);
    QLockFile guard(target + ".iisacc-lock"); lock(guard);
    (void)destination(target);
    writeUnlocked(target, encode);
}
void File::write(const QString &path, const QByteArray &bytes) {
    writeWith(path, [&](QIODevice &output) {
        if (output.write(bytes) != bytes.size()) fail(FileCode::IoError, output.errorString());
    });
}
void File::create(const QString &path, const QByteArray &bytes) {
    const auto target = destination(path);
    StagedFile staged(target);
    UnpublishedDevice temporary(staged.path());
    if (!temporary.open(QIODevice::WriteOnly) || temporary.write(bytes) != bytes.size() || !temporary.flush())
        fail(FileCode::IoError, temporary.errorString());
    temporary.close();
    const auto result = staged.publish(false);
    if (!result.succeeded) throw FileError(FileDevice::exists(target) ? FileCode::AlreadyExists : FileCode::IoError, result.message);
}
void File::update(const QString &path, const QByteArray &expected, const QByteArray &replacement) {
    const auto target = destination(path);
    QLockFile guard(target + ".iisacc-lock"); lock(guard);
    (void)destination(target);
    try {
        if (read(target, expected.size()) != expected) fail(FileCode::Conflict, "file changed outside this session");
    } catch (const FileError &error) {
        if (error.code() == FileCode::LimitExceeded || error.code() == FileCode::NotFound)
            fail(FileCode::Conflict, "file changed outside this session");
        throw;
    }
    if (expected == replacement) return;
    writeUnlocked(target, [&](QIODevice &output) {
        if (output.write(replacement) != replacement.size()) fail(FileCode::IoError, output.errorString());
    });
}
bool File::remove(const QString &path) {
    const auto target = destination(path);
    QLockFile guard(target + ".iisacc-lock"); lock(guard);
    (void)destination(target);
    if (!FileDevice::exists(target)) return false;
    detail::cancelPresentation(std::filesystem::path(target.toStdU16String()));
    FileDevice file(target);
    if (!file.remove()) fail(FileCode::IoError, file.errorString());
    detail::removePresentation(std::filesystem::path(target.toStdU16String()));
    return true;
}
void File::copy(const QString &source, const QString &target, bool overwrite) {
    auto input = openRead(source);
    if (overwrite) {
        writeWith(target, [&](QIODevice &output) { transfer(*input, output); });
        return;
    }
    const auto absolute = destination(target);
    StagedFile staged(absolute);
    UnpublishedDevice temporary(staged.path());
    if (!temporary.open(QIODevice::WriteOnly)) fail(FileCode::IoError, temporary.errorString());
    transfer(*input, temporary);
    if (!temporary.flush()) fail(FileCode::IoError, temporary.errorString());
    temporary.close();
    const auto result = staged.publish(false);
    if (!result.succeeded) throw FileError(FileDevice::exists(absolute) ? FileCode::AlreadyExists : FileCode::IoError, result.message);
}
void File::createDirectories(const QString &path) {
    std::error_code error;std::filesystem::create_directories(std::filesystem::path(absolutePath(path).toStdU16String()),error);
    if (error) fail(FileCode::IoError, QString::fromStdString(error.message()));
}

void File::copyToUrl(QIODevice &source, const QUrl &target) {
    if (!source.isOpen() || !source.isReadable()) fail(FileCode::InvalidPath, "a readable provider device is required");
    if (target.isLocalFile()) {
        writeWith(target.toLocalFile(), [&](QIODevice &output) { transfer(source, output); });
        return;
    }
#ifdef Q_OS_ANDROID
    if (target.scheme() == "content" && !target.authority().isEmpty()) {
        SaveFile output(target.toString());
        // A granted DocumentsProvider URI cannot publish by sibling rename.
        output.setDirectWriteFallback(true);
        if (!output.open(QIODevice::WriteOnly)) fail(FileCode::IoError, output.errorString());
        transfer(source, output);
        if (!output.commit()) fail(FileCode::IoError, output.errorString());
        return;
    }
#endif
    fail(FileCode::InvalidPath, "a native file URL or granted Android document URI is required");
}
void File::publishDirectory(const QString &temporary, const QString &path) {
    const auto source = absolutePath(temporary), target = absolutePath(path);
    const auto statusInfo=std::filesystem::symlink_status(std::filesystem::path(source.toStdU16String()));
    if (std::filesystem::is_symlink(statusInfo) || !std::filesystem::is_directory(statusInfo))
        fail(FileCode::InvalidPath, "source must be a real directory");
#if defined(__APPLE__)
    const auto status = renamex_np(FileDevice::encodeName(source).constData(), FileDevice::encodeName(target).constData(), RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
    const auto status = syscall(SYS_renameat2, AT_FDCWD, FileDevice::encodeName(source).constData(),
                                AT_FDCWD, FileDevice::encodeName(target).constData(), 1);
#elif defined(_WIN32)
    if (MoveFileExW(reinterpret_cast<LPCWSTR>(source.utf16()), reinterpret_cast<LPCWSTR>(target.utf16()), 0)) return;
    const auto error = GetLastError();
    fail(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? FileCode::AlreadyExists : FileCode::IoError,
         "cannot publish directory without replacement");
#else
    fail(FileCode::IoError, "exclusive directory publication is unavailable");
#endif
#if defined(__APPLE__) || (defined(__linux__) && defined(SYS_renameat2))
    if (status != 0) fail(errno == EEXIST || errno == ENOTEMPTY ? FileCode::AlreadyExists : FileCode::IoError,
                          "cannot publish directory without replacement");
#endif
}
} // namespace iiFileProvider
