#pragma once

#include <cstddef>
#include <string>

namespace Luwow::Fs::Hash {
    // The algorithms by the names scripts give them
    inline const char* const Algorithms[] = { "md5", "blake2b-128", "blake2b-256", nullptr };

    // Lowercase hex digests
    std::string md5(const std::string& data);
    std::string blake2b(const std::string& data, size_t bytes);

    // The digest by its index in Algorithms
    inline std::string digest(const std::string& data, int algorithm) {
        if (algorithm == 0) return md5(data);
        return blake2b(data, algorithm == 1 ? 16 : 32);
    }
} // namespace Luwow::Fs::Hash
