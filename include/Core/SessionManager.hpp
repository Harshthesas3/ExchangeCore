#pragma once

#include "Core/SessionConfig.hpp"
#include <string>
#include <cstdint>
#include <chrono>
#include <memory>
#include <mutex>

namespace Exchange {

class SessionManager {
public:
    explicit SessionManager(const SessionConfig& config);

    // Determines if we are running in a strict seeded match mode
    bool isMatchMode() const { return config_.is_active_session(); }

    // Random Number Generator (xoshiro256**)
    uint64_t nextRandom();
    
    // Deterministic ID generation
    std::string nextOrderID();

    // Simulated Clock
    std::chrono::system_clock::time_point now();
    
    // Returns raw simulated nanoseconds since epoch (for headless timing)
    uint64_t getSimulatedTimeNs() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return simulated_time_ns_;
    }

    const SessionConfig& getConfig() const { return config_; }
    SessionConfig& getConfig() { return config_; }

private:
    SessionConfig config_;

    // PRNG state
    uint64_t s_[4];

    // Sequence for deterministic IDs
    uint64_t seq_{0};

    // Simulated time
    uint64_t simulated_time_ns_{0}; // Since epoch

    mutable std::mutex mtx_;

    void initPRNG(const std::string& seed_hex);
};

} // namespace Exchange
