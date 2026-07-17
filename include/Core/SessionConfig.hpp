#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <set>

namespace Exchange {

struct SessionConfig {
    std::string match_id;
    std::string seed_hex;
    double capital{100000.0};
    std::vector<std::string> symbols{"AAPL", "MSFT", "GOOG", "BTCUSD", "ETHUSD"};
    std::set<std::string> symbol_universe;
    std::string opens_at;
    std::string closes_at;
    int duration_seconds{0};
    
    std::string engine_version;
    std::string generator_version;

    std::string export_path;
    std::string export_session_id;

    bool is_active_session() const {
        return !seed_hex.empty();
    }

    bool has_symbol(const std::string& sym) const {
        if (!is_active_session()) return true; // If no session, allow all or rely on engine's default.
        return symbol_universe.find(sym) != symbol_universe.end();
    }

    static SessionConfig parse(int argc, char** argv);
    static bool validateSeed(const std::string& seed);
};

} // namespace Exchange
