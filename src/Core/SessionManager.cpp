#include "Core/SessionManager.hpp"
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <vector>

namespace Exchange {

static inline uint64_t rotl(const uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

SessionManager::SessionManager(const SessionConfig& config) : config_(config) {
    if (config_.is_active_session()) {
        initPRNG(config_.seed_hex);
        
        // Parse opens_at to set initial simulated_time_ns_, or default to epoch
        // Simple fallback if no opens_at provided: start at 1_000_000_000
        simulated_time_ns_ = 1000000000ULL; 
    }
}

void SessionManager::initPRNG(const std::string& seed_hex) {
    // We expect seed_hex to be 16, 32, or 64 chars of hex
    // Map it to 4 uint64_t words.
    std::vector<uint64_t> words;
    for (size_t i = 0; i < seed_hex.length(); i += 16) {
        std::string chunk = seed_hex.substr(i, 16);
        words.push_back(std::stoull(chunk, nullptr, 16));
    }
    
    if (words.empty()) {
        s_[0] = 1; s_[1] = 2; s_[2] = 3; s_[3] = 4;
    } else {
        s_[0] = words[0];
        s_[1] = words.size() > 1 ? words[1] : words[0] ^ 0x9E3779B97F4A7C15ULL;
        s_[2] = words.size() > 2 ? words[2] : s_[0] ^ 0xBF58476D1CE4E5B9ULL;
        s_[3] = words.size() > 3 ? words[3] : s_[1] ^ 0x94D049BB133111EBULL;
    }
    
    // Ensure state is not entirely zero
    if (s_[0] == 0 && s_[1] == 0 && s_[2] == 0 && s_[3] == 0) {
        s_[0] = 1;
    }
}

uint64_t SessionManager::nextRandom() {
    std::lock_guard<std::mutex> lock(mtx_);
    const uint64_t result = rotl(s_[1] * 5, 7) * 9;

    const uint64_t t = s_[1] << 17;

    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];

    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);

    return result;
}

std::string SessionManager::nextOrderID() {
    std::lock_guard<std::mutex> lock(mtx_);
    std::stringstream ss;
    ss << "o" << seq_++;
    return ss.str();
}

std::chrono::system_clock::time_point SessionManager::now() {
    if (!isMatchMode()) {
        return std::chrono::system_clock::now();
    }
    
    // Deterministically advance time by PRNG-drawn [1, 1000] nanoseconds.
    // Note: nextRandom() takes mtx_, so we must NOT hold mtx_ when calling it.
    uint64_t r = nextRandom(); // acquires + releases mtx_

    uint64_t advance = 1 + (r % 1000);
    
    uint64_t ts_ns;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        simulated_time_ns_ += advance;
        ts_ns = simulated_time_ns_;
    }

    // system_clock::time_point duration may not be nanoseconds on all platforms
    // (e.g., MSVC uses 100ns ticks). Use duration_cast to be portable.
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::nanoseconds(ts_ns)
        )
    );
}

} // namespace Exchange
