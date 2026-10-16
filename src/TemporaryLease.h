#pragma once
#include <filesystem>
#include <memory>

namespace iiFileProvider {
// Coordinate managed temporary files in one existing, direct directory.
// Writers hold Shared before creating a temporary until after its removal.
// Cleaners may remove orphan temporaries only while TryExclusive is locked.
// The zero-byte .iisacc-temporary.lock sentinel remains for a stable lock inode.
class TemporaryLease final {
public:
    enum class Mode { Shared, TryExclusive };
    TemporaryLease(const std::filesystem::path &directory, Mode mode);
    ~TemporaryLease();
    TemporaryLease(TemporaryLease &&) noexcept;
    TemporaryLease &operator=(TemporaryLease &&) noexcept;
    TemporaryLease(const TemporaryLease &) = delete;
    TemporaryLease &operator=(const TemporaryLease &) = delete;
    // TryExclusive contention returns false; invalid paths and I/O throw.
    [[nodiscard]] bool locked() const noexcept;
private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};
}
