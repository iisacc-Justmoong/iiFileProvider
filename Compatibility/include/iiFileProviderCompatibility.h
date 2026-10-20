#pragma once
#include <filesystem>
#include <iiFileProviderNative.h>
#include <memory>
#include <string_view>

namespace iiFileProvider::native {
// Interoperates with the existing FileProvider .iisacc-lock protocol without
// Qt.
class FileWriteLock final {
public:
  explicit FileWriteLock(const std::filesystem::path &destination);
  ~FileWriteLock();
  FileWriteLock(const FileWriteLock &) = delete;
  FileWriteLock &operator=(const FileWriteLock &) = delete;

private:
  struct Private;
  std::unique_ptr<Private> p_;
};
void writeCompatible(const std::filesystem::path &, std::string_view);
} // namespace iiFileProvider::native
