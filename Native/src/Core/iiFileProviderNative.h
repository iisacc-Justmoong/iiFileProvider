#pragma once
#include "iiFileProviderNativeExport.h"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace iiFileProvider::native {
using Timestamp = std::string; // Canonical UTC ISO-8601, with millisecond precision.
[[nodiscard]] IIFILEPROVIDER_NATIVE_EXPORT Timestamp utcNow();
[[nodiscard]] IIFILEPROVIDER_NATIVE_EXPORT std::string sha256(std::string_view bytes);
struct IisaccAccount {
    std::string sub, email, displayName, userId, societyCloudMembership, avatarUrl;
};
struct AuthorMetadata {
    IisaccAccount account;
    std::string serviceOrigin, capturedAt;
};

class IIFILEPROVIDER_NATIVE_EXPORT FileAuthor final {
  public:
    [[nodiscard]] static std::optional<FileAuthor>
    fromIisaccAccount(const std::map<std::string, std::string> &account, std::string serviceOrigin,
                      Timestamp capturedAt = utcNow());
    // Credential-free schema-v1 profile. Nested details/attribution/device
    // survive unchanged.
    [[nodiscard]] static std::optional<FileAuthor> fromJsonText(std::string_view snapshot);
    [[nodiscard]] const AuthorMetadata &metadata() const noexcept { return metadata_; }
    [[nodiscard]] const std::string &toJsonText() const noexcept { return profile_; }
    [[nodiscard]] std::string displayLabel() const;
    friend bool operator==(const FileAuthor &a, const FileAuthor &b) {
        return a.profile_ == b.profile_;
    }

  private:
    AuthorMetadata metadata_;
    std::string profile_;
};
struct IIFILEPROVIDER_NATIVE_EXPORT FileLink {
    std::string name, url;
    [[nodiscard]] static std::optional<FileLink> create(std::string name, std::string url);
    [[nodiscard]] static std::optional<FileLink> fromString(std::string_view entry);
    friend bool operator==(const FileLink &, const FileLink &) = default;
};
class IIFILEPROVIDER_NATIVE_EXPORT Authorship final {
  public:
    static constexpr int SchemaVersion = 3;
    static constexpr std::size_t MaximumBytes = 2 * 1024 * 1024;
    static constexpr std::size_t MaximumLinks = 64;
    static constexpr const char *MetadataKey = "iisacc:authorship";
    Authorship();
    [[nodiscard]] static std::optional<Authorship> fromDump(std::string_view bytes);
    bool setAuthor(const FileAuthor &, Timestamp at = utcNow());
    bool setAuthor(const FileAuthor &, const std::vector<FileLink> &, Timestamp at = utcNow());
    bool setLinks(const std::vector<FileLink> &, Timestamp at = utcNow());
    bool setLinksFromStrings(const std::vector<std::string> &, Timestamp at = utcNow());
    void clearActiveAuthor() noexcept;
    [[nodiscard]] bool hasActiveAuthor() const noexcept;
    [[nodiscard]] std::optional<FileAuthor> firstEditor() const;
    [[nodiscard]] std::vector<FileAuthor> participants() const;
    [[nodiscard]] std::vector<FileLink> links() const;
    void recordChange(Timestamp at = utcNow());
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
    [[nodiscard]] bool isEmpty() const noexcept;
    [[nodiscard]] const std::string &dump() const noexcept { return dump_; }

  private:
    struct Record {
        std::string key, profile, firstChangedAt, lastChangedAt;
    };
    std::uint64_t revision_{};
    std::string modifiedAt_, lastAuthor_, activeAuthor_, dump_;
    std::vector<Record> authors_;
    std::vector<FileLink> links_;
    void rebuildDump();
};
enum class FileCode { IoFailure, LimitExceeded, AlreadyExists, Conflict, InvalidPath };
class IIFILEPROVIDER_NATIVE_EXPORT FileError : public std::runtime_error {
    FileCode code_;

  public:
    explicit FileError(std::string message, FileCode code = FileCode::IoFailure)
        : std::runtime_error(std::move(message)), code_(code) {}
    [[nodiscard]] FileCode code() const noexcept { return code_; }
};
class IIFILEPROVIDER_NATIVE_EXPORT ReadFile final {
  public:
    explicit ReadFile(const std::filesystem::path &);
    ~ReadFile();
    [[nodiscard]] std::int64_t read(char *data, std::int64_t count);
    [[nodiscard]] std::string read(std::int64_t count);
    [[nodiscard]] bool seek(std::uint64_t offset);
    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] std::uint64_t pos() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct PublishResult {
    bool succeeded{};
    std::string diagnosticSuffix, message;
};
class IIFILEPROVIDER_NATIVE_EXPORT File final {
  public:
    [[nodiscard]] static std::filesystem::path pathString(const std::filesystem::path &path) {
        return path;
    }
    [[nodiscard]] static std::unique_ptr<ReadFile> openRead(const std::filesystem::path &);
    [[nodiscard]] static std::string read(const std::filesystem::path &,
                                          std::uint64_t maximumBytes = UINT64_MAX);
    static void write(const std::filesystem::path &, std::string_view);
    static void write(const std::filesystem::path &, std::span<const std::byte>);
    // Atomic exclusive publication, and optimistic replacement of known bytes.
    // update detects changes before publication; external writers are not locked.
    static void create(const std::filesystem::path &, std::string_view);
    static void update(const std::filesystem::path &, std::string_view expected,
                       std::string_view replacement);
    [[nodiscard]] static bool remove(const std::filesystem::path &);
    static void createDirectories(const std::filesystem::path &);
    [[nodiscard]] static PublishResult publish(const std::filesystem::path &temporary,
                                               const std::filesystem::path &destination);
};
class IIFILEPROVIDER_NATIVE_EXPORT StagedFile final {
  public:
    explicit StagedFile(const std::filesystem::path &destination);
    ~StagedFile();
    [[nodiscard]] const std::filesystem::path &path() const noexcept;
    [[nodiscard]] PublishResult publish();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class IIFILEPROVIDER_NATIVE_EXPORT TemporaryDirectory final {
  public:
    explicit TemporaryDirectory(
        const std::filesystem::path &parent = std::filesystem::current_path());
    ~TemporaryDirectory();
    [[nodiscard]] const std::filesystem::path &path() const noexcept { return path_; }
    [[nodiscard]] bool isValid() const noexcept { return !path_.empty(); }
    // Relinquish cleanup after the directory has been published or transferred.
    void release() noexcept { path_.clear(); }

  private:
    std::filesystem::path path_;
};
} // namespace iiFileProvider::native
