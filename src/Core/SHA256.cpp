// SHA256.cpp — FIPS 180-4 compliant implementation, verified against NIST test vectors.

#include "Core/SHA256.hpp"
#include <cstring>
#include <sstream>
#include <iomanip>

namespace Exchange {

namespace {

// SHA-256 round constants (first 32 bits of the fractional parts of the cube
// roots of the first 64 primes).
static constexpr uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

inline uint32_t rotr32(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

inline uint32_t ch(uint32_t e, uint32_t f, uint32_t g)  { return (e & f) ^ (~e & g); }
inline uint32_t maj(uint32_t a, uint32_t b, uint32_t c) { return (a & b) ^ (a & c) ^ (b & c); }
inline uint32_t ep0(uint32_t a) { return rotr32(a, 2)  ^ rotr32(a, 13) ^ rotr32(a, 22); }
inline uint32_t ep1(uint32_t e) { return rotr32(e, 6)  ^ rotr32(e, 11) ^ rotr32(e, 25); }
inline uint32_t sig0(uint32_t x){ return rotr32(x, 7)  ^ rotr32(x, 18) ^ (x >> 3);  }
inline uint32_t sig1(uint32_t x){ return rotr32(x, 17) ^ rotr32(x, 19) ^ (x >> 10); }

} // anonymous namespace

// SHA-256 initial hash values (first 32 bits of the fractional parts of the
// square roots of the first 8 primes).
SHA256::SHA256() {
    datalen_ = 0;
    bitlen_  = 0;
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
}

void SHA256::transform() {
    uint32_t w[64];

    // Load 16 big-endian 32-bit words from data_[]
    for (int i = 0; i < 16; ++i) {
        int j = i * 4;
        w[i] = (static_cast<uint32_t>(data_[j    ]) << 24)
             | (static_cast<uint32_t>(data_[j + 1]) << 16)
             | (static_cast<uint32_t>(data_[j + 2]) <<  8)
             |  static_cast<uint32_t>(data_[j + 3]);
    }

    // Extend to 64 words
    for (int i = 16; i < 64; ++i)
        w[i] = sig1(w[i - 2]) + w[i - 7] + sig0(w[i - 15]) + w[i - 16];

    // Initialize working variables
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

    // 64 compression rounds
    for (int i = 0; i < 64; ++i) {
        uint32_t t1 = h + ep1(e) + ch(e, f, g) + K[i] + w[i];
        uint32_t t2 = ep0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void SHA256::update(const uint8_t* data, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        data_[datalen_++] = data[i];
        if (datalen_ == 64) {
            transform();
            bitlen_ += 512;
            datalen_ = 0;
        }
    }
}

void SHA256::update(const std::string& data) {
    update(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string SHA256::hex_digest() {
    // Accumulate bit length before we modify datalen_
    uint64_t total_bits = bitlen_ + static_cast<uint64_t>(datalen_) * 8;

    // Append the '1' bit (0x80 byte)
    data_[datalen_++] = 0x80;

    // If we don't have room for the 8-byte length field, flush an extra block
    if (datalen_ > 56) {
        while (datalen_ < 64)
            data_[datalen_++] = 0x00;
        transform();
        datalen_ = 0;
    }
    // Pad to 56 bytes
    while (datalen_ < 56)
        data_[datalen_++] = 0x00;

    // Append 64-bit big-endian bit count
    data_[56] = static_cast<uint8_t>(total_bits >> 56);
    data_[57] = static_cast<uint8_t>(total_bits >> 48);
    data_[58] = static_cast<uint8_t>(total_bits >> 40);
    data_[59] = static_cast<uint8_t>(total_bits >> 32);
    data_[60] = static_cast<uint8_t>(total_bits >> 24);
    data_[61] = static_cast<uint8_t>(total_bits >> 16);
    data_[62] = static_cast<uint8_t>(total_bits >>  8);
    data_[63] = static_cast<uint8_t>(total_bits      );
    transform();

    // Produce hex string (big-endian byte order)
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 8; ++i) {
        ss << std::setw(2) << ((state_[i] >> 24) & 0xffu)
           << std::setw(2) << ((state_[i] >> 16) & 0xffu)
           << std::setw(2) << ((state_[i] >>  8) & 0xffu)
           << std::setw(2) << ( state_[i]         & 0xffu);
    }
    return ss.str();
}

} // namespace Exchange
