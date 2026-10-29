#include "StagedFile.h"
#include "TemporaryLease.h"
#include "iiFileProviderDevice.h"

namespace iiFileProvider {
class StagedFile::Impl {
public:
    QString destination;
    QString path;
    std::unique_ptr<TemporaryLease> lease;
};
StagedFile::StagedFile(const QString &destination) : m_impl(std::make_unique<Impl>()) {
    m_impl->destination = File::absolutePath(destination);
    const auto directory = std::filesystem::canonical(std::filesystem::path(m_impl->destination.toStdU16String()).parent_path());
    m_impl->lease = std::make_unique<TemporaryLease>(
        directory, TemporaryLease::Mode::Shared);
    auto pattern = directory / ".iifile-stage-XXXXXX";
    pattern += std::filesystem::path(m_impl->destination.toStdU16String()).extension();
    TemporaryFile temporary(File::pathString(pattern));
    if (!temporary.open()) throw FileError(FileCode::IoError, temporary.errorString().toStdString());
    m_impl->path = temporary.fileName();
    temporary.setAutoRemove(false);
    // The provider closes its descriptor before handing the path to a codec.
}
StagedFile::~StagedFile() {
    // Rename publication leaves no source; link publication may leave one if
    // its first unlink failed. Remove the owned stage before releasing its lease.
    std::error_code ignored;
    std::filesystem::remove(std::filesystem::path(m_impl->path.toStdU16String()), ignored);
}
const QString &StagedFile::path() const noexcept { return m_impl->path; }
AtomicFileCommitResult StagedFile::publish(bool overwrite) {
    const auto result = File::publish(std::filesystem::path(m_impl->path.toStdU16String()),
        std::filesystem::path(m_impl->destination.toStdU16String()), overwrite);
    return result;
}
} // namespace iiFileProvider
