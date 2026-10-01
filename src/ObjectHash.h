#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#endif

namespace iiFileProvider::detail {
class ObjectHash {
public:
    ObjectHash();
    void update(std::span<const std::uint8_t> bytes);
    void update(std::string_view text) {
        update({reinterpret_cast<const std::uint8_t *>(text.data()), text.size()});
    }
    std::string finish();
    static std::string digest(std::span<const std::uint8_t> bytes) { ObjectHash h; h.update(bytes); return h.finish(); }
private:
#ifdef __APPLE__
    CC_SHA256_CTX context{};
#else
    void block(const std::uint8_t *data);
    std::array<std::uint32_t, 8> state{0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::array<std::uint8_t, 64> pending{};
    std::uint64_t length = 0;
    std::size_t used = 0;
#endif
};
}
