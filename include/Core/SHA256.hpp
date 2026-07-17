#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace Exchange {

class SHA256 {
public:
    SHA256();
    void update(const uint8_t* data, size_t length);
    void update(const std::string& data);
    std::string hex_digest();

private:
    uint8_t data_[64];
    uint32_t datalen_;
    uint64_t bitlen_;
    uint32_t state_[8];

    void transform();
};

} // namespace Exchange
