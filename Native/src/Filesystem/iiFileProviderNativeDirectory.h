#pragma once

#include "iiFileProviderNative.h"

namespace iiFileProvider::native {
// Atomically move a real staged directory to a new sibling name. Existing
// paths, including dangling symlinks, are never replaced. Cross-directory and
// cross-volume publication are rejected; on failure the stage remains owned by
// the caller. Platforms without exclusive directory rename fail closed.
IIFILEPROVIDER_NATIVE_EXPORT PublishResult publishDirectoryExclusive(
    const std::filesystem::path &stage, const std::filesystem::path &destination);
}
