#pragma once
#include "ObjectSource.h"
#include "ObjectStore.h"

namespace iiFileProvider::detail {
// Internal bounded input stage. No SQLite connection is touched by its workers.
struct PreparedObjectSource {
    std::filesystem::path source;
    ObjectSourceIdentity identity;
    std::vector<std::uint8_t> bytes;
};
std::vector<PreparedObjectSource> readObjectBatch(const std::vector<ObjectImport> &files,
                                                 std::size_t concurrency=0);
}
