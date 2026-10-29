#pragma once
#include "ObjectStore.h"
#include "ObjectSource.h"
#include <functional>
#include <stop_token>

namespace iiFileProvider {
struct ObjectTreeMapping {
    std::string prefix;
    std::filesystem::path root;
    std::vector<std::string> excludedTopLevel;
};
struct ObjectPackagingEntry {
    std::string logicalPath;
    std::filesystem::path source;
    ObjectSourceIdentity identity;
};
struct ObjectPackagingIssue {std::string path;std::string message;};
struct ObjectInventory {
    std::vector<ObjectPackagingEntry> entries;
    std::vector<ObjectPackagingIssue> issues;
    std::uint64_t totalBytes=0;
};
struct ObjectPackagingReport {
    std::uint64_t imported=0,revised=0,skipped=0;
    std::vector<ObjectPackagingIssue> issues;
    bool cancelled=false;
};
struct ObjectAuditReport {
    std::uint64_t verified=0;
    std::vector<ObjectPackagingIssue> issues;
    bool cancelled=false;
};
class ObjectPackager final {
public:
    static ObjectInventory inventory(const std::vector<ObjectTreeMapping> &mappings,
                                     std::stop_token cancellation={},std::size_t concurrency=0);
    explicit ObjectPackager(ObjectStore &store);
    // Resumes from committed per-file source stamps. A failed/cancelled file is
    // rolled back; previous successful files remain committed and enumerable.
    // Supplied authorship is reconciled even for unchanged source bytes: nullopt
    // preserves the document, explicit empty clears it, and changes create a
    // revision of the existing object. Readers may be called again on retry.
    ObjectPackagingReport package(const ObjectInventory &,const std::string &sessionKey,
        std::stop_token cancellation={},
        const std::function<void(const ObjectPackagingEntry &,const ObjectRecord &,const char *)> &progress={},
        const std::function<std::optional<ObjectMetadata>(const ObjectPackagingEntry &)> &authorship={},
        std::size_t maximumBatchFiles=1);
    // Verify every mapped source against its current immutable payload/history
    // and compact index. Small snapshots/reads are bounded and parallel; large
    // sources stream. This does not freeze a changing filesystem or all pages.
    ObjectAuditReport audit(const ObjectInventory &,std::stop_token cancellation={},
        const std::function<void(const ObjectPackagingEntry &,const std::string &)> &progress={}) const;
private:
    std::filesystem::path stagingDirectory() const;
    ObjectStore &m_store;
    std::filesystem::path m_directory;
};
}
