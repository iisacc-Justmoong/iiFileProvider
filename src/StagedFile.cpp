#include "StagedFile.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>

namespace iiFileProvider {
class StagedFile::Impl {
public:
    QString destination;
    QString path;
    bool published = false;
};
StagedFile::StagedFile(const QString &destination) : m_impl(std::make_unique<Impl>()) {
    m_impl->destination = File::absolutePath(destination);
    QTemporaryFile temporary(QFileInfo(m_impl->destination).dir().filePath(".iifile-stage-XXXXXX"));
    if (!temporary.open()) throw FileError(FileCode::IoError, temporary.errorString().toStdString());
    m_impl->path = temporary.fileName();
    temporary.setAutoRemove(false);
    // Destruction closes the native handle; QTemporaryFile::close() does not.
}
StagedFile::~StagedFile() {
    if (!m_impl->published) QFile::remove(m_impl->path);
}
const QString &StagedFile::path() const noexcept { return m_impl->path; }
AtomicFileCommitResult StagedFile::publish(bool overwrite) {
    const auto result = File::publish(std::filesystem::path(m_impl->path.toStdU16String()),
        std::filesystem::path(m_impl->destination.toStdU16String()), overwrite);
    if (result.succeeded) m_impl->published = true;
    return result;
}
} // namespace iiFileProvider
