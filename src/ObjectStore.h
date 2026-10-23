#pragma once

#include "FileCatalog.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace iiFileProvider {

// Exact, credential-free output of FileAuthor::toJson or Authorship::dump.
// An empty document means unknown provenance, never "the importer is creator".
struct ObjectMetadata {
    std::string format;
    std::string payload;
    bool operator==(const ObjectMetadata &) const = default;
};

// Persistent attribution, never a login token or an authentication claim.
struct ObjectAuthor {
    std::string origin;
    std::string subject;
    std::string displayName;
    ObjectMetadata profile;
    bool operator==(const ObjectAuthor &) const = default;
};

struct ObjectRecord {
    std::string key;
    std::int64_t indexKey = 0;
    std::uint64_t version = 0;
    std::string path;
    std::string sha256;
    std::uint64_t size = 0;
    ObjectAuthor author;
    std::string sessionKey;
    std::int64_t recordedAtNs = 0;
    std::string operation;
    bool deleted = false;
    std::string parentValidationKey;
    std::string validationKey;
    ObjectMetadata authorship;
    std::string sourceStamp;
    std::uint32_t schemaVersion = 2;
};

struct ObjectWriteOptions {
    // Omission preserves existing authorship; explicit empty clears to unknown.
    std::optional<ObjectMetadata> authorship;
    std::string sourceStamp;
    std::stop_token cancellation;
};

struct ObjectImport {
    std::filesystem::path source;
    std::string logicalPath;
    ObjectWriteOptions options;
};

// Compact persistent traversal projection, atomically updated with each head.
// Full author documents and journal/payload are fetched on demand; whole-index
// scans neither join history nor copy those potentially large data.
struct ObjectIndexEntry {
    std::string key;
    std::int64_t indexKey=0;
    std::uint64_t version=0;
    std::string path;
    std::string sha256;
    std::uint64_t size=0;
    std::string validationKey;
    std::string sessionKey;
    bool deleted=false;
    std::string authorOrigin;
    std::string authorSubject;
    bool operator==(const ObjectIndexEntry &) const = default;
};

// A reversible binary diff. Hashes address immutable chunk bytes in the package.
// An empty hash denotes absence (insertion/deletion); offset is a byte offset.
struct ObjectChange {
    std::uint64_t offset = 0;
    std::string beforeHash;
    std::string afterHash;
    std::uint64_t beforeSize = 0;
    std::uint64_t afterSize = 0;
};

struct ObjectSession {
    std::string key;
    ObjectAuthor author;
    std::string device;
    std::string description;
    std::int64_t startedAtNs = 0;
    std::int64_t endedAtNs = 0;
};

// Immutable session context. Authentication credentials do not belong here.
struct ObjectSessionContext {
    std::string application, applicationVersion, project, document;
    std::map<std::string, std::string> attributes;
    bool operator==(const ObjectSessionContext &) const = default;
};
struct WorkSessionRecord {
    std::int64_t sequence = 0;
    ObjectSession session;
    ObjectSessionContext context;
};
struct ObjectSessionQuery {
    std::int64_t afterSequence = 0;
    std::size_t limit = 64;
    bool activeOnly = false;
};
struct ObjectJournalQuery {
    std::int64_t afterSequence = 0;
    std::size_t limit = 256;
    std::string objectKey, sessionKey;
};
// Compact projection; payload, author profiles and annotations are fetched only
// on demand. sequence is a persistent keyset cursor, not a wall-clock ordering.
struct ObjectJournalEntry {
    std::int64_t sequence = 0;
    std::string objectKey;
    std::uint64_t version = 0;
    std::string sessionKey;
    std::int64_t recordedAtNs = 0;
    std::string operation, path, sha256;
    std::uint64_t size = 0;
    bool deleted = false;
    std::string validationKey;
};
struct RawMetadata {
    std::string originalName;
    std::string mediaType = "application/octet-stream";
    FileMetadata annotations;
    bool operator==(const RawMetadata &) const = default;
};
struct RawEnvelope {
    ObjectRecord record;
    RawMetadata metadata;
};
struct ObjectDocument {
    ObjectRecord file; // Mutable working object with its own stable identity.
    RawEnvelope original; // Immutable original bytes; annotations may be revised.
};
struct MetadataChange {
    std::string pointer; // JSON Pointer relative to the FileMetadata document.
    std::optional<std::string> beforeJson, afterJson; // Absence differs from null.
};

// Qt-independent C++23 storage owner. The directory contains a transactional
// SQLite object package; the source tree remains an ordinary, unmodified tree.
// One instance serializes its connection. Separate instances support WAL readers.
class ObjectStore final {
public:
    enum class Access { ReadOnly, ReadWrite, Create };
    static constexpr std::size_t chunkBytes = 1024 * 1024;
    static constexpr std::size_t maximumBatchFiles = 256;
    static constexpr std::uint64_t maximumBatchBytes = 16ull * 1024 * 1024;
    static constexpr std::size_t maximumBatchMetadataBytes = 16 * 1024 * 1024;
    static constexpr int defaultWalAutoCheckpointPages = 1000;
    explicit ObjectStore(const std::filesystem::path &directory,
                         std::string containerKey, bool create = false);
    ObjectStore(const std::filesystem::path &directory, std::string containerKey,
                bool create, int walAutoCheckpointPages);
    // ReadOnly accepts schemas 3/4/5 and never migrates or configures writer
    // checkpoints. Journal/RAW/session-context APIs require schema >=4. A writer
    // migrates earlier packages transactionally, preserving legacy record hashes.
    ObjectStore(const std::filesystem::path &directory, std::string containerKey,
                Access access, int walAutoCheckpointPages = defaultWalAutoCheckpointPages);
    ~ObjectStore();
    ObjectStore(const ObjectStore &) = delete;
    ObjectStore &operator=(const ObjectStore &) = delete;

    ObjectSession beginSession(ObjectAuthor author, std::string device, std::string description);
    ObjectSession beginSession(ObjectAuthor author, std::string device, std::string description,
                               const ObjectSessionContext &context);
    void endSession(const std::string &sessionKey);
    ObjectSession session(const std::string &sessionKey) const;
    WorkSessionRecord workSession(const std::string &sessionKey) const;
    std::vector<WorkSessionRecord> sessions(const ObjectSessionQuery &query = {}) const;
    std::vector<ObjectJournalEntry> journal(const ObjectJournalQuery &query = {}) const;
    const std::filesystem::path &directory() const noexcept;
    const std::string &containerKey() const noexcept;
    Access access() const noexcept; // Create normalizes to ReadWrite capability.

    // Captures any original byte file, including camera RAW, without decoding or
    // changing it. Captured payload is immutable; only metadata/move/erase may
    // produce further revisions. extract() returns the original bytes.
    ObjectRecord wrapRaw(const std::filesystem::path &source, std::string logicalPath,
                         const RawMetadata &metadata, const std::string &sessionKey,
                         const ObjectWriteOptions &options = {});
    RawEnvelope rawEnvelope(const std::string &key, std::uint64_t version = 0) const;
    // Atomically capture an immutable RAW original and a mutable working object.
    // Both reference the same immutable chunks, without a second payload ingest.
    // The reserved .originals/<working-key> path retains the original after moves
    // and deletion. Consumers should use original() rather than that path.
    ObjectDocument importDocument(const std::filesystem::path &source, std::string logicalPath,
        const RawMetadata &metadata, const std::string &sessionKey,
        const ObjectWriteOptions &options = {});
    RawEnvelope original(const std::string &documentKey) const;
    ObjectRecord reviseMetadata(const std::string &key, const FileMetadata &metadata,
                               std::uint64_t expectedVersion, const std::string &sessionKey);
    std::vector<MetadataChange> metadataDiff(const std::string &key, std::uint64_t version) const;

    ObjectRecord importFile(const std::filesystem::path &source, std::string logicalPath,
                            const std::string &sessionKey, const ObjectWriteOptions &options = {});
    // Small-file ingestion: at most 256 files / 16 MiB payload and 16 MiB
    // author/provenance JSON (including repeated actor profiles), one commit.
    // Bounded workers read payloads; the owning thread publishes in input order.
    // All records become visible together; any failure rolls back the batch.
    std::vector<ObjectRecord> importFiles(const std::vector<ObjectImport> &files,
                                          const std::string &sessionKey);
    ObjectRecord reviseFile(const std::string &key, const std::filesystem::path &source,
                            std::uint64_t expectedVersion, const std::string &sessionKey,
                            const ObjectWriteOptions &options = {});
    ObjectRecord move(const std::string &key, std::string logicalPath,
                      std::uint64_t expectedVersion, const std::string &sessionKey);
    // Rebind a caller-verified native relocation without rereading payload.
    // sourceStamp is caller metadata; it is not a content-verification proof.
    ObjectRecord move(const std::string &key, std::string logicalPath,
        std::uint64_t expectedVersion, const std::string &sessionKey, const ObjectWriteOptions &options);
    ObjectRecord erase(const std::string &key, std::uint64_t expectedVersion,
                       const std::string &sessionKey);

    std::optional<ObjectRecord> lookup(const std::string &key) const;
    std::optional<ObjectRecord> lookupPath(const std::string &logicalPath) const;
    // Keyset pagination: no recursive filesystem scan and no OFFSET pagination.
    std::vector<ObjectRecord> scan(std::int64_t afterIndexKey = 0, std::size_t limit = 256,
                                  bool includeDeleted = false) const;
    std::vector<ObjectIndexEntry> index(std::int64_t afterIndexKey = 0, std::size_t limit = 1024,
                                       bool includeDeleted = false) const;
    std::vector<ObjectRecord> history(const std::string &key, std::uint64_t afterVersion = 0,
                                     std::size_t limit = 256) const;
    std::vector<ObjectChange> diff(const std::string &key, std::uint64_t version) const;
    // Schema/integrity evidence only; not a signature or authorization credential.
    bool validate(const std::string &key, std::uint64_t version, bool payload = true) const;
    // Exclusive creation: never overwrites an existing file or restores a tombstone.
    void extract(const std::string &key, std::uint64_t version,
                 const std::filesystem::path &destination) const;
    std::uint64_t count(bool includeDeleted = false) const;

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace iiFileProvider
