#include <axonvex/types/primitives/primitives.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstring>  // For std::memcpy

namespace axonvex::types::primitives {

    // UUID Implementation
    UUID::UUID() {
        data_.fill(0);
    }

    UUID::UUID(const std::string& str) {
        fromString(str);
    }

    UUID UUID::generate() {
        return generateRandom();
    }

    UUID UUID::generateRandom() {
        UUID uuid;
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_int_distribution<uint8_t> dis(0, 255);
        
        for (auto& byte : uuid.data_) {
            byte = dis(gen);
        }
        
        // Set version (4) and variant bits according to RFC 4122
        uuid.data_[6] = (uuid.data_[6] & 0x0F) | 0x40;  // Version 4
        uuid.data_[8] = (uuid.data_[8] & 0x3F) | 0x80;  // Variant 10
        
        return uuid;
    }

    UUID UUID::generateTimeBasedMark() {
        UUID uuid;
        auto now = std::chrono::high_resolution_clock::now();
        auto timestamp = now.time_since_epoch().count();
        
        // Copy timestamp to first 8 bytes
        std::memcpy(uuid.data_.data(), &timestamp, sizeof(timestamp));
        
        // Fill remaining bytes with random data
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_int_distribution<uint8_t> dis(0, 255);
        
        for (size_t i = 8; i < 16; ++i) {
            uuid.data_[i] = dis(gen);
        }
        
        // Set version (1) and variant bits
        uuid.data_[6] = (uuid.data_[6] & 0x0F) | 0x10;  // Version 1
        uuid.data_[8] = (uuid.data_[8] & 0x3F) | 0x80;  // Variant 10
        
        return uuid;
    }

    std::string UUID::toString() const {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        
        for (size_t i = 0; i < 16; ++i) {
            if (i == 4 || i == 6 || i == 8 || i == 10) {
                oss << '-';
            }
            oss << std::setw(2) << static_cast<unsigned>(data_[i]);
        }
        
        return oss.str();
    }

    void UUID::fromString(const std::string& str) {
        std::string cleaned = str;
        cleaned.erase(std::remove(cleaned.begin(), cleaned.end(), '-'), cleaned.end());
        
        if (cleaned.length() != 32) {
            throw std::invalid_argument("Invalid UUID string format");
        }
        
        for (size_t i = 0; i < 16; ++i) {
            std::string byteStr = cleaned.substr(i * 2, 2);
            data_[i] = static_cast<UInt8>(std::stoul(byteStr, nullptr, 16));
        }
    }

    bool UUID::operator==(const UUID& other) const noexcept {
        return data_ == other.data_;
    }

    bool UUID::operator!=(const UUID& other) const noexcept {
        return !(*this == other);
    }

    bool UUID::operator<(const UUID& other) const noexcept {
        return data_ < other.data_;
    }

    std::size_t UUID::hash() const noexcept {
        std::size_t result = 0;
        std::hash<UInt8> hasher;
        
        for (const auto& byte : data_) {
            result ^= hasher(byte) + 0x9e3779b9 + (result << 6) + (result >> 2);
        }
        
        return result;
    }

    const UUID& UUID::null() {
        static const UUID nullUuid;
        return nullUuid;
    }

    bool UUID::isNull() const noexcept {
        return *this == null();
    }

    int UUID::version() const noexcept {
        return (data_[6] >> 4) & 0x0F;
    }

    int UUID::variant() const noexcept {
        return (data_[8] >> 6) & 0x03;
    }

    // PrecisionFloat Implementation
    bool PrecisionFloat::equals(const PrecisionFloat& other) const noexcept {
        return std::abs(value_ - other.value_) <= std::max(epsilon_, other.epsilon_);
    }

    bool PrecisionFloat::equals(double other) const noexcept {
        return std::abs(value_ - other) <= epsilon_;
    }

    PrecisionFloat PrecisionFloat::operator+(const PrecisionFloat& other) const {
        return PrecisionFloat(value_ + other.value_, std::max(epsilon_, other.epsilon_));
    }

    PrecisionFloat PrecisionFloat::operator-(const PrecisionFloat& other) const {
        return PrecisionFloat(value_ - other.value_, std::max(epsilon_, other.epsilon_));
    }

    PrecisionFloat PrecisionFloat::operator*(const PrecisionFloat& other) const {
        double result = value_ * other.value_;
        double newEpsilon = std::abs(result) * std::max(epsilon_ / std::abs(value_), 
                                                       other.epsilon_ / std::abs(other.value_));
        return PrecisionFloat(result, newEpsilon);
    }

    PrecisionFloat PrecisionFloat::operator/(const PrecisionFloat& other) const {
        if (std::abs(other.value_) <= other.epsilon_) {
            throw std::runtime_error("Division by zero in PrecisionFloat");
        }
        
        double result = value_ / other.value_;
        double newEpsilon = std::abs(result) * (epsilon_ / std::abs(value_) + 
                                               other.epsilon_ / std::abs(other.value_));
        return PrecisionFloat(result, newEpsilon);
    }

    PrecisionFloat& PrecisionFloat::operator+=(const PrecisionFloat& other) {
        value_ += other.value_;
        epsilon_ = std::max(epsilon_, other.epsilon_);
        return *this;
    }

    PrecisionFloat& PrecisionFloat::operator-=(const PrecisionFloat& other) {
        value_ -= other.value_;
        epsilon_ = std::max(epsilon_, other.epsilon_);
        return *this;
    }

    PrecisionFloat& PrecisionFloat::operator*=(const PrecisionFloat& other) {
        double newEpsilon = std::abs(value_ * other.value_) * 
                           std::max(epsilon_ / std::abs(value_), 
                                   other.epsilon_ / std::abs(other.value_));
        value_ *= other.value_;
        epsilon_ = newEpsilon;
        return *this;
    }

    PrecisionFloat& PrecisionFloat::operator/=(const PrecisionFloat& other) {
        if (std::abs(other.value_) <= other.epsilon_) {
            throw std::runtime_error("Division by zero in PrecisionFloat");
        }
        
        double newEpsilon = std::abs(value_ / other.value_) * 
                           (epsilon_ / std::abs(value_) + 
                            other.epsilon_ / std::abs(other.value_));
        value_ /= other.value_;
        epsilon_ = newEpsilon;
        return *this;
    }

    bool PrecisionFloat::operator==(const PrecisionFloat& other) const noexcept {
        return equals(other);
    }

    bool PrecisionFloat::operator!=(const PrecisionFloat& other) const noexcept {
        return !equals(other);
    }

    bool PrecisionFloat::operator<(const PrecisionFloat& other) const noexcept {
        return (value_ + epsilon_) < (other.value_ - other.epsilon_);
    }

    bool PrecisionFloat::operator<=(const PrecisionFloat& other) const noexcept {
        return (*this < other) || (*this == other);
    }

    bool PrecisionFloat::operator>(const PrecisionFloat& other) const noexcept {
        return (value_ - epsilon_) > (other.value_ + other.epsilon_);
    }

    bool PrecisionFloat::operator>=(const PrecisionFloat& other) const noexcept {
        return (*this > other) || (*this == other);
    }

} // namespace axonvex::types::primitives