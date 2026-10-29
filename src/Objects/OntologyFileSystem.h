#pragma once
#include "ObjectStore.h"
#include <utility>
#include <variant>

namespace iiFileProvider {
enum class OntologyResourceKind { File, Entity };
struct OntologyResource {
    OntologyResourceKind kind = OntologyResourceKind::Entity;
    std::string key;
    static OntologyResource file(std::string key) { return {OntologyResourceKind::File, std::move(key)}; }
    static OntologyResource entity(std::string key) { return {OntologyResourceKind::Entity, std::move(key)}; }
    bool operator==(const OntologyResource &) const = default;
};
enum class OntologyValueKind { Resource, Text, Integer, Real, Boolean };
struct OntologyValue {
    std::variant<OntologyResource, std::string, std::int64_t, double, bool> data;
    static OntologyValue resource(OntologyResource value) { return {std::move(value)}; }
    static OntologyValue text(std::string value) { return {std::move(value)}; }
    static OntologyValue integer(std::int64_t value) { return {value}; }
    static OntologyValue real(double value) { return {value}; }
    static OntologyValue boolean(bool value) { return {value}; }
    OntologyValueKind kind() const noexcept { return static_cast<OntologyValueKind>(data.index()); }
    bool operator==(const OntologyValue &) const = default;
};
struct OntologyClass {
    std::string key, label, description;
    std::vector<std::string> parents;
    bool operator==(const OntologyClass &) const = default;
};
struct OntologyProperty {
    std::string key, label;
    OntologyValueKind kind = OntologyValueKind::Text;
    std::string domainClass, rangeClass;
    bool singleValue = false, acyclic = false;
    bool operator==(const OntologyProperty &) const = default;
};
struct OntologyEntity {
    std::string key, label, description;
    bool operator==(const OntologyEntity &) const = default;
};
struct OntologyTypeAssignment {
    OntologyResource resource;
    std::string classKey;
    bool present = true;
};
struct OntologyAssertion {
    OntologyResource subject;
    std::string propertyKey;
    OntologyValue value;
    bool present = true;
};
struct OntologyCondition {
    std::string propertyKey;
    std::optional<OntologyValue> equals; // Omission tests property presence.
    bool operator==(const OntologyCondition &) const = default;
};
struct OntologyQuery {
    std::string classKey;
    bool includeSubclasses = true;
    std::vector<OntologyCondition> conditions; // AND, at most 16.
    bool includeDeleted = false;
    bool operator==(const OntologyQuery &) const = default;
};
struct OntologyDirectory {
    std::string path, label;
    OntologyQuery query;
    bool operator==(const OntologyDirectory &) const = default;
};
struct OntologyRemoveDirectory { std::string path; };
using OntologyChange = std::variant<OntologyClass, OntologyProperty, OntologyEntity,
    OntologyTypeAssignment, OntologyAssertion, OntologyDirectory, OntologyRemoveDirectory>;
struct OntologyCursor {
    std::uint64_t revision = 0;
    std::int64_t fileSequence = 0, afterIndexKey = 0;
    std::string fingerprint;
};
struct OntologyPage {
    std::uint64_t revision = 0;
    std::int64_t fileSequence = 0;
    std::vector<ObjectIndexEntry> files;
    std::optional<OntologyCursor> next;
};
struct OntologyStatement {
    std::int64_t sequence = 0;
    OntologyResource subject;
    std::string propertyKey;
    OntologyValue value;
};
enum class OntologyDirection { Outgoing, Incoming };
struct OntologyNeighbor { OntologyResource resource; unsigned depth = 0; };
struct OntologyWalk { std::vector<OntologyNeighbor> nodes; bool truncated = false; };
struct OntologyEvent {
    std::int64_t sequence = 0;
    std::uint64_t revision = 0;
    std::string operation, subject, beforeJson, afterJson, sessionKey;
    std::int64_t recordedAtNs = 0;
};

// Native semantic filesystem over the same transactional ObjectStore package.
// Owns an independent SQLite connection and copies the originating capabilities;
// the supplied ObjectStore need not outlive this facade. No OS kernel mount.
class OntologyFileSystem final {
public:
    static constexpr std::size_t maximumBatchChanges = 256;
    static constexpr std::size_t maximumBatchJournalBytes = 4 * 1024 * 1024;
    static constexpr std::size_t maximumTraversalRows = 10000;
    explicit OntologyFileSystem(const ObjectStore &store);
    ~OntologyFileSystem();
    OntologyFileSystem(const OntologyFileSystem &) = delete;
    OntologyFileSystem &operator=(const OntologyFileSystem &) = delete;
    std::uint64_t revision() const;
    // Ordered, atomic batch; definitions precede their first use. CAS includes
    // schema edits, classifications, assertions and semantic directories.
    std::uint64_t apply(const std::vector<OntologyChange> &changes, std::uint64_t expectedRevision,
                        const std::string &sessionKey, std::stop_token cancellation = {});
    std::optional<OntologyClass> ontologyClass(const std::string &key) const;
    std::vector<OntologyClass> classes(const std::string &afterKey = {}, std::size_t limit = 256) const;
    std::optional<OntologyProperty> property(const std::string &key) const;
    std::optional<OntologyEntity> entity(const std::string &key) const;
    std::vector<OntologyStatement> statements(const OntologyResource &subject,
        std::int64_t afterSequence = 0, std::size_t limit = 256) const;
    OntologyPage queryFiles(const OntologyQuery &query = {}, const OntologyCursor &cursor = {},
        std::size_t limit = 256, std::stop_token cancellation = {}) const;
    OntologyPage listDirectory(const std::string &path, const OntologyCursor &cursor = {},
        std::size_t limit = 256, std::stop_token cancellation = {}) const;
    std::vector<OntologyDirectory> directories(const std::string &prefix = {},
        const std::string &afterPath = {}, std::size_t limit = 256) const;
    OntologyWalk traverse(const OntologyResource &start, const std::string &propertyKey,
        OntologyDirection direction = OntologyDirection::Outgoing, unsigned maximumDepth = 8,
        std::size_t limit = 256, bool includeDeleted = false, std::stop_token cancellation = {}) const;
    std::vector<OntologyEvent> journal(std::int64_t afterSequence = 0, std::size_t limit = 256,
        const std::string &sessionKey = {}) const;
    void extractFile(const std::string &objectKey, std::uint64_t version,
        const std::filesystem::path &destination) const;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace iiFileProvider
