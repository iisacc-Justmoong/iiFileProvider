#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>

namespace iiFileProvider {
struct ObjectSourceIdentity {
    std::string stamp;
    std::uint64_t size=0;
    bool operator==(const ObjectSourceIdentity &) const = default;
};

// Stable staging input for the object package, not a replacement for the source.
// Apple uses a descriptor-based clone where supported. Fallback copies reject a
// changed source identity/size/mtime/ctime. All source ancestors reject symlinks.
class ObjectSource final {
public:
    static ObjectSourceIdentity inspect(const std::filesystem::path &source);
    ObjectSource(const std::filesystem::path &source,const std::filesystem::path &stagingDirectory,
                 std::stop_token cancellation={});
    // Reject stale inventories at the acquired descriptor before cloning/copying.
    ObjectSource(const std::filesystem::path &source,const std::filesystem::path &stagingDirectory,
                 std::stop_token cancellation,std::optional<ObjectSourceIdentity> expectedIdentity);
    ~ObjectSource();
    ObjectSource(const ObjectSource &)=delete;
    ObjectSource &operator=(const ObjectSource &)=delete;
    const std::filesystem::path &path() const;
    const ObjectSourceIdentity &identity() const;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
}
