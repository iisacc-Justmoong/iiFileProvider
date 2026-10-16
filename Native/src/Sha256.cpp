// Adapted from iisacc MIT engineering counterpart; full notice preserved.
#include "Sha256_p.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

namespace iiFileProvider::native::detail {
namespace {
constexpr std::array<std::uint32_t, 64> constants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};
}

void Sha256::transform(std::span<const std::byte, 64> block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
        words[i] = (std::to_integer<std::uint32_t>(block[i * 4]) << 24) |
                   (std::to_integer<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (std::to_integer<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   std::to_integer<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
        const auto a = words[i - 15], b = words[i - 2];
        const auto small0 = std::rotr(a, 7) ^ std::rotr(a, 18) ^ (a >> 3);
        const auto small1 = std::rotr(b, 17) ^ std::rotr(b, 19) ^ (b >> 10);
        words[i] = words[i - 16] + small0 + words[i - 7] + small1;
    }
    auto [a, b, c, d, e, f, g, h] = m_state;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const auto big1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto choice = (e & f) ^ (~e & g);
        const auto first = h + big1 + choice + constants[i] + words[i];
        const auto big0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto majority = (a & b) ^ (a & c) ^ (b & c);
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + big0 + majority;
    }
    m_state[0] += a;
    m_state[1] += b;
    m_state[2] += c;
    m_state[3] += d;
    m_state[4] += e;
    m_state[5] += f;
    m_state[6] += g;
    m_state[7] += h;
}

void Sha256::update(std::span<const std::byte> input) {
    constexpr auto maximumBytes = std::numeric_limits<std::uint64_t>::max() / 8;
    if (input.size() > maximumBytes - m_bytes)
        throw std::length_error("SHA-256 message exceeds its 64-bit bit length");
    m_bytes += input.size();
    if (m_pendingSize != 0) {
        const auto take = std::min(input.size(), m_pending.size() - m_pendingSize);
        std::copy_n(input.begin(), take,
                    m_pending.begin() + static_cast<std::ptrdiff_t>(m_pendingSize));
        m_pendingSize += take;
        input = input.subspan(take);
        if (m_pendingSize == 64) {
            transform(m_pending);
            m_pendingSize = 0;
        }
    }
    while (input.size() >= 64) {
        transform(input.first<64>());
        input = input.subspan(64);
    }
    if (!input.empty()) {
        std::copy(input.begin(), input.end(), m_pending.begin());
        m_pendingSize = input.size();
    }
}

std::array<std::byte, 32> Sha256::digest() const {
    auto finalized = *this;
    const auto bits = m_bytes * 8;
    auto &block = finalized.m_pending;
    auto used = finalized.m_pendingSize;
    block[used++] = std::byte{0x80};
    if (used > 56) {
        std::fill(block.begin() + static_cast<std::ptrdiff_t>(used), block.end(), std::byte{0});
        finalized.transform(block);
        used = 0;
    }
    std::fill(block.begin() + static_cast<std::ptrdiff_t>(used), block.begin() + 56, std::byte{0});
    for (std::size_t i = 0; i < 8; ++i)
        block[63 - i] = static_cast<std::byte>(bits >> (8 * i));
    finalized.transform(block);
    std::array<std::byte, 32> result{};
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t j = 0; j < 4; ++j)
            result[i * 4 + j] = static_cast<std::byte>(finalized.m_state[i] >> (24 - 8 * j));
    return result;
}

std::string Sha256::hexDigest() const {
    constexpr char digits[] = "0123456789abcdef";
    const auto value = digest();
    std::string output;
    output.reserve(64);
    for (auto byte : value) {
        const auto v = std::to_integer<unsigned int>(byte);
        output.push_back(digits[v >> 4]);
        output.push_back(digits[v & 15]);
    }
    return output;
}
std::string sha256(std::span<const std::byte> input) {
    Sha256 hash;
    hash.update(input);
    return hash.hexDigest();
}
} // namespace iiFileProvider::native::detail
