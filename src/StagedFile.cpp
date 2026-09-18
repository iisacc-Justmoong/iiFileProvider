#include "StagedFile.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>

namespace iiFileProvider {
class StagedFile::Impl {
public:
    QString destination;
    QString path;
    QTemporaryFile temporary;
};
StagedFile::StagedFile(const QString &destination) : m_impl(std::make_unique<Impl>()) {
    m_impl->destination = File::absolutePath(destination);
    m_impl->temporary.setFileTemplate(QFileInfo(m_impl->destination).dir().filePath(".iifile-stage-XXXXXX"));
    if (!m_impl->temporary.open()) throw FileError(FileCode::IoError, m_impl->temporary.errorString().toStdString());
    m_impl->path = m_impl->temporary.fileName();
    m_impl->temporary.close();
}
StagedFile::~StagedFile() = default;
const QString &StagedFile::path() const noexcept { return m_impl->path; }
AtomicFileCommitResult StagedFile::publish(bool overwrite) {
    return File::publish(std::filesystem::path(m_impl->path.toStdU16String()),
        std::filesystem::path(m_impl->destination.toStdU16String()), overwrite);
}
} // namespace iiFileProvider
