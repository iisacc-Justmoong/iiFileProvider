#pragma once
#include "Export.h"
#include <QIODevice>
#include <QDateTime>
#include <QTemporaryDir>
#include <QDir>
#include <QLockFile>
#include <memory>
#include <cstdio>
namespace iiFileProvider::native { class Device; }

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
// Qt codecs see a QIODevice; the provider owns every filesystem operation.
class IIFILEPROVIDER_EXPORT FileDevice : public QIODevice {
public:
    enum FileError { NoError, ReadError, WriteError, FatalError, ResourceError, OpenError,
        AbortError, TimeOutError, UnspecifiedError, RemoveError, RenameError,
        PositionError, ResizeError, PermissionsError, CopyError };
    enum Permission { ReadOwner=0x4000,WriteOwner=0x2000,ExeOwner=0x1000,
        ReadUser=0x0400,WriteUser=0x0200,ExeUser=0x0100,ReadGroup=0x0040,
        WriteGroup=0x0020,ExeGroup=0x0010,ReadOther=0x0004,WriteOther=0x0002,ExeOther=0x0001 };
    Q_DECLARE_FLAGS(Permissions,Permission)
    enum FileHandleFlag { DontCloseHandle=0,AutoCloseHandle=1 };
    Q_DECLARE_FLAGS(FileHandleFlags,FileHandleFlag)
    enum MemoryMapFlag { NoOptions=0,MapPrivateOption=1 };
    Q_DECLARE_FLAGS(MemoryMapFlags,MemoryMapFlag)
    explicit FileDevice(QObject* parent=nullptr);
    explicit FileDevice(const QString&,QObject* parent=nullptr);
    ~FileDevice() override;
    bool open(OpenMode mode) override;
    bool open(FILE*,OpenMode,FileHandleFlags=DontCloseHandle);
    bool open(int,OpenMode,FileHandleFlags=DontCloseHandle);
    void close() override;
    bool isSequential() const override;
    QString fileName() const;
    void setFileName(const QString&);
    FileError error() const noexcept;
    void unsetError();
    int handle() const;
    qint64 size() const override;
    qint64 pos() const override;
    bool seek(qint64) override;
    qint64 bytesAvailable() const override;
    bool atEnd() const override;
    bool flush();
    bool exists() const;
    static bool exists(const QString&);
    bool remove();
    static bool remove(const QString&);
    static bool copy(const QString &source, const QString &destination);
    bool copy(const QString &destination);
    static bool rename(const QString &source, const QString &destination);
    bool rename(const QString &destination);
    static bool resize(const QString &path, qint64 size);
    virtual bool resize(qint64 size);
    virtual Permissions permissions() const;
    static Permissions permissions(const QString&);
    virtual bool setPermissions(Permissions);
    static bool setPermissions(const QString&,Permissions);
    template<class Enum> bool setPermissions(QFlags<Enum> p) { return setPermissions(Permissions::fromInt(p.toInt())); }
    uchar* map(qint64,qint64,MemoryMapFlags=NoOptions);
    bool unmap(uchar*);
    static QByteArray encodeName(const QString&);
    static QString decodeName(const QByteArray&);
protected:
    qint64 readData(char*,qint64) override;
    qint64 writeData(const char*,qint64) override;
    bool useNativeDevice(native::Device&,OpenMode);
    void releaseNativeDevice();
    bool reserveTemporary(const QString&);
    void setPresentationEnabled(bool);
    bool syncToDisk();
    void fail(FileError,const QString&);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
class IIFILEPROVIDER_EXPORT SaveFile : public FileDevice {
public:
    explicit SaveFile(QObject* parent=nullptr);
    explicit SaveFile(const QString&,QObject* parent=nullptr);
    ~SaveFile() override;
    bool open(OpenMode mode) override;
    void close() override;
    void cancelWriting();
    bool commit();
    bool resize(qint64) override;
    void setDirectWriteFallback(bool);
    bool directWriteFallback() const;
    bool setPermissions(Permissions) override;
    using FileDevice::setPermissions;
private:
    class Impl;
    std::unique_ptr<Impl> m_save;
};
class IIFILEPROVIDER_EXPORT TemporaryFile : public FileDevice {
public:
    explicit TemporaryFile(QObject* parent=nullptr);
    explicit TemporaryFile(const QString&,QObject* parent=nullptr);
    ~TemporaryFile() override;
    bool open();
    bool autoRemove() const;
    void setAutoRemove(bool);
    QString fileTemplate() const;
    void setFileTemplate(const QString&);
private:
    QString m_pattern;
    bool m_autoRemove=true,m_reserved=false;
};
class IIFILEPROVIDER_EXPORT TemporaryDirectory : public QTemporaryDir {
public:
    TemporaryDirectory();
    explicit TemporaryDirectory(const QString &pattern);
    ~TemporaryDirectory();
    bool remove();
};
} // namespace iiFileProvider
Q_DECLARE_OPERATORS_FOR_FLAGS(iiFileProvider::FileDevice::Permissions)
Q_DECLARE_OPERATORS_FOR_FLAGS(iiFileProvider::FileDevice::FileHandleFlags)
Q_DECLARE_OPERATORS_FOR_FLAGS(iiFileProvider::FileDevice::MemoryMapFlags)
