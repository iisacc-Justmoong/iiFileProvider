#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace iiFileProvider::native::detail {
// Independently implemented SHA-256, following NIST FIPS 180-4 sections 5/6.
class Sha256 {
  public:
    void update(std::span<const std::byte> input);
    [[nodiscard]] std::array<std::byte, 32> digest() const;
    [[nodiscard]] std::string hexDigest() const;

  private:
    void transform(std::span<const std::byte, 64> block);
    std::array<std::uint32_t, 8> m_state{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                         0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
    std::array<std::byte, 64> m_pending{};
    std::uint64_t m_bytes{};
    std::size_t m_pendingSize{};
};
[[nodiscard]] std::string sha256(std::span<const std::byte> input);
} // namespace iiFileProvider::native::detail
