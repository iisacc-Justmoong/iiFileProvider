#pragma once
#include "iiFileProviderNative.h"
#include <cstdio>

namespace iiFileProvider::native {
struct OpenOptions {
    bool read = false, write = false, append = false, truncate = false;
    bool exclusive = false, existing = false, noFollow = false;
    std::filesystem::perms permissions = std::filesystem::perms(0666);
};

// Owns native descriptor operations. A borrowed descriptor/FILE remains the
// caller's property unless takeOwnership is explicitly requested.
class IIFILEPROVIDER_NATIVE_EXPORT Device final {
public:
    explicit Device(bool presentOnClose = true);
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    void open(const std::filesystem::path&, OpenOptions = {.read = true});
    void openTemporary(const std::filesystem::path& pattern);
    void attach(int descriptor, bool takeOwnership, OpenOptions = {.read = true});
    void attach(FILE*, bool takeOwnership, OpenOptions = {.read = true});
    void close();
    bool isOpen() const noexcept;
    bool failed() const noexcept;
    bool isSequential() const noexcept;
    int handle() const noexcept;
    const std::filesystem::path& path() const noexcept;
    std::int64_t read(char*, std::int64_t);
    std::int64_t write(const char*, std::int64_t);
    bool seek(std::int64_t);
    std::int64_t pos() const;
    std::int64_t size() const;
    bool resize(std::int64_t);
    void flush(bool durable = false);
    std::filesystem::perms permissions() const;
    bool setPermissions(std::filesystem::perms);
    char* map(std::int64_t offset, std::int64_t bytes, bool privateCopy = false);
    bool unmap(char*);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Publication never silently falls back to an in-place write. An explicit
// permission override is used by AtomicSave's setPermissions contract.
IIFILEPROVIDER_NATIVE_EXPORT PublishResult publishFile(
    const std::filesystem::path& stage, const std::filesystem::path& destination,
    bool overwrite = true,
    std::optional<std::filesystem::perms> permissions = {});
IIFILEPROVIDER_NATIVE_EXPORT PublishResult renameFileExclusive(
    const std::filesystem::path& source, const std::filesystem::path& destination);

class IIFILEPROVIDER_NATIVE_EXPORT AtomicSave final {
public:
    AtomicSave();
    ~AtomicSave();
    AtomicSave(const AtomicSave&) = delete;
    AtomicSave& operator=(const AtomicSave&) = delete;
    void open(const std::filesystem::path&);
    Device& device() noexcept;
    const std::filesystem::path& stagedPath() const noexcept;
    void cancel() noexcept;
    bool setPermissions(std::filesystem::perms);
    PublishResult commit(bool overwrite = true);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace iiFileProvider::native
