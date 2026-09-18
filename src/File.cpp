#include "File.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <algorithm>
#include <cerrno>
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
FileError::FileError(FileCode code, const std::string &message)
    : std::runtime_error(message), m_code(code) {}
FileCode FileError::code() const noexcept { return m_code; }
namespace {
[[noreturn]] void fail(FileCode code, const QString &message) {
    throw FileError(code, message.toStdString());
}
QString destination(const QString &path) {
    const auto absolute = File::absolutePath(path);
    const QFileInfo info(absolute);
    if (info.isSymLink() || (info.exists() && !info.isFile()))
        fail(FileCode::InvalidPath, "destination must be a regular file, not a link or directory");
    return absolute;
}
void lock(QLockFile &file) {
    if (!file.tryLock(0)) fail(file.error() == QLockFile::LockFailedError ? FileCode::Conflict : FileCode::IoError,
                              "cannot acquire ownership of this path");
}
void writeUnlocked(const QString &path, const std::function<void(QIODevice &)> &encode) {
    if (!encode) fail(FileCode::InvalidPath, "a file encoder is required");
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) fail(FileCode::IoError, file.errorString());
    encode(file);
    if (!file.commit()) fail(FileCode::IoError, file.errorString());
}
void transfer(QIODevice &input, QIODevice &output) {
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty()) fail(FileCode::IoError, input.errorString());
        if (output.write(bytes) != bytes.size()) fail(FileCode::IoError, output.errorString());
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
    return QFileInfo(path).absoluteFilePath();
}
std::unique_ptr<QIODevice> File::openRead(const QString &path) {
    const auto absolute = absolutePath(path);
    const QFileInfo info(absolute);
    if (!info.exists()) fail(FileCode::NotFound, "file does not exist");
    if (!info.isFile()) fail(FileCode::InvalidPath, "input must be a regular file");
    auto file = std::make_unique<QFile>(absolute);
    if (!file->open(QIODevice::ReadOnly)) fail(FileCode::IoError, file->errorString());
    return file;
}
QByteArray File::read(const QString &path, qint64 maximumBytes) {
    if (maximumBytes < 0 || maximumBytes >= std::numeric_limits<qsizetype>::max())
        fail(FileCode::LimitExceeded, "invalid file read limit");
    auto file = openRead(path);
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
    auto file = openRead(path);
    const auto bytes = file->read(count);
    if (bytes.size() < count && !file->atEnd()) fail(FileCode::IoError, file->errorString());
    return bytes;
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
    QTemporaryFile temporary(QFileInfo(target).dir().filePath(".iifile-XXXXXX"));
    if (!temporary.open() || temporary.write(bytes) != bytes.size() || !temporary.flush())
        fail(FileCode::IoError, temporary.errorString());
    temporary.close();
    const auto result = publish(std::filesystem::path(temporary.fileName().toStdU16String()),
                                std::filesystem::path(target.toStdU16String()), false);
    if (!result.succeeded) throw FileError(QFileInfo::exists(target) ? FileCode::AlreadyExists : FileCode::IoError, result.message);
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
    if (!QFileInfo::exists(target)) return false;
    QFile file(target);
    if (!file.remove()) fail(FileCode::IoError, file.errorString());
    return true;
}
void File::copy(const QString &source, const QString &target, bool overwrite) {
    auto input = openRead(source);
    if (overwrite) {
        writeWith(target, [&](QIODevice &output) { transfer(*input, output); });
        return;
    }
    const auto absolute = destination(target);
    QTemporaryFile temporary(QFileInfo(absolute).dir().filePath(".iifile-XXXXXX"));
    if (!temporary.open()) fail(FileCode::IoError, temporary.errorString());
    transfer(*input, temporary);
    if (!temporary.flush()) fail(FileCode::IoError, temporary.errorString());
    temporary.close();
    const auto result = publish(std::filesystem::path(temporary.fileName().toStdU16String()),
                                std::filesystem::path(absolute.toStdU16String()), false);
    if (!result.succeeded) throw FileError(QFileInfo::exists(absolute) ? FileCode::AlreadyExists : FileCode::IoError, result.message);
}
void File::createDirectories(const QString &path) {
    if (!QDir().mkpath(absolutePath(path))) fail(FileCode::IoError, "cannot create directory");
}
void File::publishDirectory(const QString &temporary, const QString &path) {
    const auto source = absolutePath(temporary), target = absolutePath(path);
    if (QFileInfo(source).isSymLink() || !QFileInfo(source).isDir())
        fail(FileCode::InvalidPath, "source must be a real directory");
#if defined(__APPLE__)
    const auto status = renamex_np(QFile::encodeName(source).constData(), QFile::encodeName(target).constData(), RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
    const auto status = syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(source).constData(),
                                AT_FDCWD, QFile::encodeName(target).constData(), 1);
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
