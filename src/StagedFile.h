#pragma once
#include "File.h"

namespace iiFileProvider {
// A provider-owned private path for codecs (ZIP, FFmpeg, LibreOffice) that need
// filesystem access. Only publish() makes its contents the destination file.
class IIFILEPROVIDER_EXPORT StagedFile final {
public:
    explicit StagedFile(const QString &destination);
    ~StagedFile();
    StagedFile(const StagedFile &) = delete;
    StagedFile &operator=(const StagedFile &) = delete;
    const QString &path() const noexcept;
    AtomicFileCommitResult publish(bool overwrite = true);
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace iiFileProvider
