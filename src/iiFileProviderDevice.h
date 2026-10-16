#pragma once
#include "Export.h"
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QDir>
#include <QLockFile>

namespace iiFileProvider {
class IIFILEPROVIDER_EXPORT LockFile : public QLockFile {
public:
    using QLockFile::QLockFile;
    bool lock();
    bool tryLock(int timeout = 0);
};
class IIFILEPROVIDER_EXPORT Directory : public QDir {
public:
    using QDir::QDir;
    Directory() = default;
    Directory(const QDir &value) : QDir(value) {}
    bool mkpath(const QString &path) const;
    bool mkdir(const QString &path) const;
};
// Device adapters retain Qt's error, permission, append and exclusive-create
// contracts while placing filesystem opening in the storage owner. Use File's
// atomic writers for complete documents and SaveFile for streaming replacements.
class IIFILEPROVIDER_EXPORT FileDevice : public QFile {
public:
    using QFile::QFile;
    ~FileDevice() override;
    bool open(OpenMode mode) override;
    void close() override;
    using QFile::open; // Borrowed descriptors do not create files.
    static bool copy(const QString &source, const QString &destination);
    bool copy(const QString &destination);
    static bool rename(const QString &source, const QString &destination);
    bool rename(const QString &destination);
    static bool resize(const QString &path, qint64 size);
    bool resize(qint64 size) override;
};
class IIFILEPROVIDER_EXPORT SaveFile : public QSaveFile {
public:
    using QSaveFile::QSaveFile;
    bool open(OpenMode mode) override;
    bool commit();
};
class IIFILEPROVIDER_EXPORT TemporaryFile : public QTemporaryFile {
public:
    using QTemporaryFile::QTemporaryFile;
    bool open();
};
class IIFILEPROVIDER_EXPORT TemporaryDirectory : public QTemporaryDir {
public:
    TemporaryDirectory();
    explicit TemporaryDirectory(const QString &pattern);
    ~TemporaryDirectory();
    bool remove();
};
} // namespace iiFileProvider
