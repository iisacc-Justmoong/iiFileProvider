#include "StagedFile.h"
#include "TemporaryLease.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

namespace iiFileProvider {
class StagedFile::Impl {
public:
    QString destination;
    QString path;
    std::unique_ptr<TemporaryLease> lease;
};
StagedFile::StagedFile(const QString &destination) : m_impl(std::make_unique<Impl>()) {
    m_impl->destination = File::absolutePath(destination);
    const auto directory = QFileInfo(QFileInfo(m_impl->destination).absolutePath()).canonicalFilePath();
    m_impl->lease = std::make_unique<TemporaryLease>(
        std::filesystem::path(directory.toStdU16String()), TemporaryLease::Mode::Shared);
    QTemporaryFile temporary(QFileInfo(m_impl->destination).dir().filePath(".iifile-stage-XXXXXX"));
    if (!temporary.open()) throw FileError(FileCode::IoError, temporary.errorString().toStdString());
    m_impl->path = temporary.fileName();
    temporary.setAutoRemove(false);
    // Destruction closes the native handle; QTemporaryFile::close() does not.
}
StagedFile::~StagedFile() {
    // Rename publication leaves no source; link publication may leave one if
    // its first unlink failed. Remove the owned stage before releasing its lease.
    QFile::remove(m_impl->path);
}
const QString &StagedFile::path() const noexcept { return m_impl->path; }
AtomicFileCommitResult StagedFile::publish(bool overwrite) {
    const auto result = File::publish(std::filesystem::path(m_impl->path.toStdU16String()),
        std::filesystem::path(m_impl->destination.toStdU16String()), overwrite);
    return result;
}
} // namespace iiFileProvider
