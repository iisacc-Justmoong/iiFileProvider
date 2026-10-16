#pragma once
#include "iiFileProviderPresentation.h"
#include <string_view>
namespace iiFileProvider::native::detail {
std::optional<FilePreview> imagePreview(std::string_view, FileDetails &);
#ifdef __APPLE__
std::optional<FilePreview> applePreview(const std::filesystem::path &, FileDetails &);
#endif
}
