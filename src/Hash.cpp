#include "Hash.h"

#include <cstdint>
#include <cstring>

namespace Luwow::Fs::Hash {
    static uint32_t read32(const uint8_t* p) {
        return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
            (static_cast<uint32_t>(p[3]) << 24);
    }

    static uint64_t read64(const uint8_t* p) {
        return static_cast<uint64_t>(read32(p)) | (static_cast<uint64_t>(read32(p + 4)) << 32);
    }

    static std::string toHex(const uint8_t* bytes, size_t count) {
        static const char digits[] = "0123456789abcdef";
        std::string hex(count * 2, '\0');
        for (size_t i = 0; i < count; ++i) {
            hex[i * 2] = digits[bytes[i] >> 4];
            hex[i * 2 + 1] = digits[bytes[i] & 0xF];
        }
        return hex;
    }

    // MD5 (RFC 1321)

    std::string md5(const std::string& data) {
        static constexpr uint32_t K[64] = { 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
            0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
            0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6,
            0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681,
            0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085,
            0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
            0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82,
            0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
        static constexpr uint8_t R[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5,
            9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10,
            15, 21, 6, 10, 15, 21 };

        uint32_t h[4] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476 };
        auto block = [&](const uint8_t* chunk) {
            uint32_t m[16];
            for (int i = 0; i < 16; ++i) m[i] = read32(chunk + i * 4);
            uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
            for (int i = 0; i < 64; ++i) {
                uint32_t f;
                int g;
                if (i < 16) {
                    f = (b & c) | (~b & d);
                    g = i;
                } else if (i < 32) {
                    f = (d & b) | (~d & c);
                    g = (5 * i + 1) % 16;
                } else if (i < 48) {
                    f = b ^ c ^ d;
                    g = (3 * i + 5) % 16;
                } else {
                    f = c ^ (b | ~d);
                    g = (7 * i) % 16;
                }
                uint32_t rotated = a + f + K[i] + m[g];
                a = d;
                d = c;
                c = b;
                b += (rotated << R[i]) | (rotated >> (32 - R[i]));
            }
            h[0] += a;
            h[1] += b;
            h[2] += c;
            h[3] += d;
        };

        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
        size_t full = data.size() / 64 * 64;
        for (size_t i = 0; i < full; i += 64) block(bytes + i);

        uint8_t tail[128] = {};
        size_t rest = data.size() - full;
        memcpy(tail, bytes + full, rest);
        tail[rest] = 0x80;
        size_t tailLength = rest + 1 + 8 <= 64 ? 64 : 128;
        uint64_t bits = static_cast<uint64_t>(data.size()) * 8;
        for (int i = 0; i < 8; ++i) tail[tailLength - 8 + i] = static_cast<uint8_t>(bits >> (8 * i));
        for (size_t i = 0; i < tailLength; i += 64) block(tail + i);

        uint8_t digest[16];
        for (int i = 0; i < 4; ++i) {
            for (int k = 0; k < 4; ++k) digest[i * 4 + k] = static_cast<uint8_t>(h[i] >> (8 * k));
        }
        return toHex(digest, 16);
    }

    // BLAKE2b (RFC 7693), unkeyed

    std::string blake2b(const std::string& data, size_t bytes) {
        static constexpr uint64_t IV[8] = { 0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL, 0x3c6ef372fe94f82bULL,
            0xa54ff53a5f1d36f1ULL, 0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL, 0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL };
        static constexpr uint8_t Sigma[12][16] = {
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
            { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
            { 11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4 },
            { 7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8 },
            { 9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13 },
            { 2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9 },
            { 12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11 },
            { 13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10 },
            { 6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5 },
            { 10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0 },
            { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
            { 14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3 },
        };
        if (bytes < 1 || bytes > 64) bytes = 64;

        uint64_t h[8];
        for (int i = 0; i < 8; ++i) h[i] = IV[i];
        h[0] ^= 0x01010000ULL ^ bytes;

        auto rotr = [](uint64_t x, int n) { return (x >> n) | (x << (64 - n)); };
        auto compress = [&](const uint8_t* chunk, uint64_t counter, bool final) {
            uint64_t m[16], v[16];
            for (int i = 0; i < 16; ++i) m[i] = read64(chunk + i * 8);
            for (int i = 0; i < 8; ++i) {
                v[i] = h[i];
                v[i + 8] = IV[i];
            }
            v[12] ^= counter;
            if (final) v[14] = ~v[14];
            auto mix = [&](int a, int b, int c, int d, uint64_t x, uint64_t y) {
                v[a] = v[a] + v[b] + x;
                v[d] = rotr(v[d] ^ v[a], 32);
                v[c] = v[c] + v[d];
                v[b] = rotr(v[b] ^ v[c], 24);
                v[a] = v[a] + v[b] + y;
                v[d] = rotr(v[d] ^ v[a], 16);
                v[c] = v[c] + v[d];
                v[b] = rotr(v[b] ^ v[c], 63);
            };
            for (int round = 0; round < 12; ++round) {
                const uint8_t* s = Sigma[round];
                mix(0, 4, 8, 12, m[s[0]], m[s[1]]);
                mix(1, 5, 9, 13, m[s[2]], m[s[3]]);
                mix(2, 6, 10, 14, m[s[4]], m[s[5]]);
                mix(3, 7, 11, 15, m[s[6]], m[s[7]]);
                mix(0, 5, 10, 15, m[s[8]], m[s[9]]);
                mix(1, 6, 11, 12, m[s[10]], m[s[11]]);
                mix(2, 7, 8, 13, m[s[12]], m[s[13]]);
                mix(3, 4, 9, 14, m[s[14]], m[s[15]]);
            }
            for (int i = 0; i < 8; ++i) h[i] ^= v[i] ^ v[i + 8];
        };

        const uint8_t* input = reinterpret_cast<const uint8_t*>(data.data());
        size_t length = data.size();
        uint64_t counter = 0;
        // Every block but the last is compressed as it comes, the last one is flagged final
        while (length > 128) {
            counter += 128;
            compress(input, counter, false);
            input += 128;
            length -= 128;
        }
        uint8_t last[128] = {};
        memcpy(last, input, length);
        counter += length;
        compress(last, counter, true);

        uint8_t digest[64];
        for (int i = 0; i < 8; ++i) {
            for (int k = 0; k < 8; ++k) digest[i * 8 + k] = static_cast<uint8_t>(h[i] >> (8 * k));
        }
        return toHex(digest, bytes);
    }
} // namespace Luwow::Fs::Hash
