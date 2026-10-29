#pragma once
#include "ObjectSource.h"
#include <vector>

namespace iiFileProvider::detail {
struct ObjectSnapshotRequest {
    std::filesystem::path source;
    ObjectSourceIdentity identity;
};

// Private batch owner: acquisition and destruction overlap filesystem waits.
// No worker uses SQLite or calls application metadata/progress callbacks.
class ObjectSnapshotBatch final {
public:
    ObjectSnapshotBatch(const std::vector<ObjectSnapshotRequest> &requests,
                        const std::filesystem::path &staging,
                        std::stop_token cancellation={},std::size_t concurrency=0);
    ~ObjectSnapshotBatch();
    ObjectSnapshotBatch(const ObjectSnapshotBatch &)=delete;
    ObjectSnapshotBatch &operator=(const ObjectSnapshotBatch &)=delete;
    const std::filesystem::path &path(std::size_t index) const;
private:
    void clear() noexcept;
    std::vector<std::unique_ptr<ObjectSource>> m_sources;
    std::size_t m_concurrency=1;
};
}
