#pragma once

#include <string>
#include <random>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <algorithm>

namespace axonvex::types::primitives {

class UUID {
public:
    // Constructs an empty (invalid) UUID
    UUID() : high_(0), low_(0) {}
    UUID(uint64_t high, uint64_t low) : high_(high), low_(low) {}

    static UUID generate() {
        // Use a local RNG seeded from random_device to avoid ODR issues
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        uint64_t h = dist(gen);
        uint64_t l = dist(gen);
        // Set UUID version (v4) and variant bits
        // Version: set 4 high bits of time_hi_and_version
        l &= 0xFFFFFFFFFFFF0FFFULL;
        l |= 0x0000000000004000ULL;
        // Variant: 10xx in the high bits of clock_seq_hi_and_reserved
        l &= 0x3FFFFFFFFFFFFFFFULL;
        l |= 0x8000000000000000ULL;
        return UUID(h, l);
    }

    static UUID fromString(const std::string& s) {
        // Accept 36-char with hyphens or 32 hex chars without hyphens
        std::string hex;
        hex.reserve(32);
        for (char c : s) {
            if (c == '-' || c == '{' || c == '}') continue;
            hex.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        if (hex.size() != 32) return UUID();
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            return -1;
        };
        uint64_t parts[2] {0, 0};
        for (size_t i = 0; i < 32; ++i) {
            int n = nibble(hex[i]);
            if (n < 0) return UUID();
            size_t idx = (i < 16) ? 0 : 1;
            parts[idx] = (parts[idx] << 4) | static_cast<uint64_t>(n);
        }
        return UUID(parts[0], parts[1]);
    }

    std::string toString(bool withHyphens = true) const {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0') << std::nouppercase;
        auto write64 = [&](uint64_t v) {
            oss << std::setw(16) << v;
        };
        // Compose canonical 8-4-4-4-12 from the two 64-bit values
        // Extract 32 high bits of high_, then 16, 16, 16 from low_/high_, then 48 from low_
        uint32_t time_low = static_cast<uint32_t>(high_ >> 32);
        uint16_t time_mid = static_cast<uint16_t>((high_ >> 16) & 0xFFFFULL);
        uint16_t time_hi_and_version = static_cast<uint16_t>(high_ & 0xFFFFULL);
        uint16_t clock_seq = static_cast<uint16_t>(low_ >> 48);
        uint64_t node = (low_ & 0x0000FFFFFFFFFFFFULL);

        auto write_hex = [&](uint64_t v, int width) {
            oss << std::setw(width) << (v & ((width >= 16) ? 0xFFFFFFFFFFFFFFFFULL : ((1ULL << (width*4)) - 1)));
        };

        write_hex(time_low, 8);
        if (withHyphens) oss << '-';
        write_hex(time_mid, 4);
        if (withHyphens) oss << '-';
        write_hex(time_hi_and_version, 4);
        if (withHyphens) oss << '-';
        write_hex(clock_seq, 4);
        if (withHyphens) oss << '-';
        write_hex(node, 12);
        return oss.str();
    }

    bool isValid() const { return high_ != 0 || low_ != 0; }

    bool operator==(const UUID& other) const { return high_ == other.high_ && low_ == other.low_; }
    bool operator!=(const UUID& other) const { return !(*this == other); }
    bool operator<(const UUID& other) const { return (high_ < other.high_) || (high_ == other.high_ && low_ < other.low_); }

    uint64_t high() const { return high_;
    }
    uint64_t low() const { return low_; }

private:
    uint64_t high_;
    uint64_t low_;
};

} // namespace axonvex::types::primitives
