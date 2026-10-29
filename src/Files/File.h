#pragma once

#include "Export.h"
#include <QByteArray>
#include <QIODevice>
#include <QString>
#include <QUrl>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

namespace iiFileProvider {

enum class FileCode { InvalidPath, NotFound, AlreadyExists, Conflict, LimitExceeded, CorruptFile, IoError, TimedOut };

class IIFILEPROVIDER_EXPORT FileError : public std::runtime_error {
public:
    FileError(FileCode code, const std::string &message);
    [[nodiscard]] FileCode code() const noexcept;
private:
    FileCode m_code;
};

struct AtomicFileCommitResult {
    bool succeeded = false;
    std::string diagnosticSuffix;
    std::string message;
};

// Format-independent filesystem owner. Codecs consume/produce bytes or devices;
// no document, renderer, account-service, or transport dependency belongs here.
class IIFILEPROVIDER_EXPORT File final {
public:
    static QString absolutePath(const QString &path);
    static QString pathString(const std::filesystem::path &path);
    static std::unique_ptr<QIODevice> openRead(const QString &path);
    static QByteArray read(const QString &path,
        qint64 maximumBytes = std::numeric_limits<qsizetype>::max() - 1);
    static QByteArray readPrefix(const QString &path, qint64 bytes);
    static void create(const QString &path, const QByteArray &bytes);
    // Reserve an empty database before a caller-owned Qt SQL schema/driver opens
    // it. Existing contents are retained; symlinks and directories are rejected.
    static bool prepareDatabase(const QString &path);
    // Atomic create-or-replace. An encoder exception discards all staged bytes.
    static void write(const QString &path, const QByteArray &bytes);
    static void writeWith(const QString &path, const std::function<void(QIODevice &)> &encode);
    // Caller-owned image codecs encode to a provider-owned atomic stream.
    template<class Image>
    static bool saveImage(const Image &image, const QString &path, const char *format = nullptr, int quality = -1) {
        const auto inferred = imageFormat(path);
        try {
            writeWith(path, [&](QIODevice &device) {
                if (!image.save(&device, format ? format : inferred.constData(), quality))
                    throw FileError(FileCode::IoError, "Image encoder failed");
            });
            return true;
        } catch (const FileError &) { return false; }
    }
    // Compare-and-replace, serialized with other File mutations across processes.
    // Non-cooperating external writers still require application-level coordination.
    static void update(const QString &path, const QByteArray &expected, const QByteArray &replacement);
    static bool remove(const QString &path);
    static void copy(const QString &source, const QString &destination, bool overwrite = false);
    // Publishes a caller-provided read device. Native destinations are atomic;
    // Android picker content URIs use the explicitly granted document provider.
    static void copyToUrl(QIODevice &source, const QUrl &destination);
    // Final publication for reviewed codecs that require a private temporary path.
    // Temporary and destination must reside on the same filesystem.
    static AtomicFileCommitResult publish(const std::filesystem::path &temporary,
        const std::filesystem::path &destination, bool overwrite = true);
    static void createDirectories(const QString &path);
    static void publishDirectory(const QString &temporary, const QString &destination);
private:
    static QByteArray imageFormat(const QString &path);
    File() = delete;
};
} // namespace iiFileProvider
