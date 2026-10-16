#include "iiFileProviderDevice.h"
#include "File.h"
#include "Presentation_p.h"
namespace iiFileProvider {
namespace { void present(const QString &path) { detail::presentLater(std::filesystem::path(path.toStdU16String())); } }
bool LockFile::lock() { return QLockFile::lock(); }
bool LockFile::tryLock(int timeout) { return QLockFile::tryLock(timeout); }
bool Directory::mkpath(const QString &path) const { return QDir::mkpath(path); }
bool Directory::mkdir(const QString &path) const { return QDir::mkdir(path); }
bool FileDevice::open(OpenMode mode) { return QFile::open(mode); }
FileDevice::~FileDevice() { close(); }
void FileDevice::close() {
    const auto path = fileName();
    const bool complete = isOpen() && (openMode() & QIODevice::WriteOnly) && flush() && error() == QFileDevice::NoError;
    QFile::close();
    if (complete) present(path);
}
bool FileDevice::copy(const QString &source, const QString &destination) {
    // Preserve the legacy device's permissions, attributes and Qt diagnostics.
    const bool copied = QFile::copy(source, destination); if (copied) present(destination); return copied;
}
bool FileDevice::copy(const QString &destination) { const bool copied=QFile::copy(destination); if (copied) present(destination); return copied; }
bool FileDevice::rename(const QString &source,const QString &destination) { const bool renamed=QFile::rename(source,destination); if (renamed) present(destination); return renamed; }
bool FileDevice::rename(const QString &destination) { const bool renamed=QFile::rename(destination); if (renamed) present(destination); return renamed; }
bool FileDevice::resize(const QString &path,qint64 size) { return QFile::resize(path,size); }
bool FileDevice::resize(qint64 size) { return QFile::resize(size); }
bool SaveFile::open(OpenMode mode) { return QSaveFile::open(mode); }
bool SaveFile::commit() { const auto path=fileName(); const bool committed=QSaveFile::commit(); if (committed) present(path); return committed; }
bool TemporaryFile::open() { return QTemporaryFile::open(); }
TemporaryDirectory::TemporaryDirectory() = default;
TemporaryDirectory::TemporaryDirectory(const QString &pattern) : QTemporaryDir(pattern) {}
TemporaryDirectory::~TemporaryDirectory() { if (autoRemove()) detail::cancelPresentation(std::filesystem::path(path().toStdU16String())); }
bool TemporaryDirectory::remove() { detail::cancelPresentation(std::filesystem::path(path().toStdU16String())); return QTemporaryDir::remove(); }
} // namespace iiFileProvider
