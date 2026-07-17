#include "Core/SessionConfig.hpp"
#include <iostream>
#include <fstream>
#include <regex>
#include <cstdlib>
#include <sstream>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#pragma comment(lib, "shell32.lib")
#endif

namespace Exchange {

bool SessionConfig::validateSeed(const std::string& seed) {
    std::regex seed_regex("^[0-9a-fA-F]{16}|[0-9a-fA-F]{32}|[0-9a-fA-F]{64}$");
    return std::regex_match(seed, seed_regex);
}

static std::string getAppDataPath() {
#ifdef _WIN32
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path))) {
        return std::string(path) + "\\ExchangeCore";
    }
#else
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.config/ExchangeCore";
    }
#endif
    return ".";
}

// Minimal manual JSON parser for session.json (to avoid large dependencies)
static void parseJsonConfig(SessionConfig& config, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    // Very basic parsing
    auto extractString = [&](const std::string& key) -> std::optional<std::string> {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return std::nullopt;
        pos = content.find(":", pos);
        if (pos == std::string::npos) return std::nullopt;
        size_t start = content.find("\"", pos);
        if (start == std::string::npos) return std::nullopt;
        size_t end = content.find("\"", start + 1);
        if (end == std::string::npos) return std::nullopt;
        return content.substr(start + 1, end - start - 1);
    };

    auto extractNumber = [&](const std::string& key) -> std::optional<double> {
        size_t pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return std::nullopt;
        pos = content.find(":", pos);
        if (pos == std::string::npos) return std::nullopt;
        size_t start = content.find_first_of("0123456789.-", pos);
        if (start == std::string::npos) return std::nullopt;
        size_t end = content.find_first_not_of("0123456789.-", start);
        return std::stod(content.substr(start, end - start));
    };

    if (auto v = extractString("seed"); v) config.seed_hex = *v;
    if (auto v = extractString("match_id"); v) config.match_id = *v;
    if (auto v = extractString("opens_at"); v) config.opens_at = *v;
    if (auto v = extractString("closes_at"); v) config.closes_at = *v;
    if (auto v = extractNumber("capital"); v) config.capital = *v;
    if (auto v = extractNumber("duration_seconds"); v) config.duration_seconds = static_cast<int>(*v);
    
    // Symbols is an array, harder to parse manually, but we can do a simple search if needed.
    // Assuming format "symbols": ["AAPL", "MSFT"]
    size_t sym_pos = content.find("\"symbols\"");
    if (sym_pos != std::string::npos) {
        size_t start_arr = content.find("[", sym_pos);
        size_t end_arr = content.find("]", sym_pos);
        if (start_arr != std::string::npos && end_arr != std::string::npos) {
            std::string arr = content.substr(start_arr, end_arr - start_arr);
            std::regex word_regex("\"([^\"]+)\"");
            auto words_begin = std::sregex_iterator(arr.begin(), arr.end(), word_regex);
            auto words_end = std::sregex_iterator();
            
            std::vector<std::string> symbols;
            for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
                symbols.push_back((*i)[1].str());
            }
            if (!symbols.empty()) {
                config.symbols = symbols;
            }
        }
    }
}

SessionConfig SessionConfig::parse(int argc, char** argv) {
    SessionConfig config;

#ifdef ENGINE_VERSION
    config.engine_version = ENGINE_VERSION;
#else
    config.engine_version = "1.0.0";
#endif

#ifdef GENERATOR_VERSION
    config.generator_version = GENERATOR_VERSION;
#else
    config.generator_version = "1.0.0";
#endif

    // Fallback: Read from config file first
    std::string config_path = getAppDataPath() + "\\session.json";
    parseJsonConfig(config, config_path);

    // CLI overrides
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) {
            std::string seed = argv[++i];
            if (seed.rfind("0x", 0) == 0) seed = seed.substr(2); // strip 0x
            config.seed_hex = seed;
        } else if (arg == "--capital" && i + 1 < argc) {
            config.capital = std::stod(argv[++i]);
        } else if (arg == "--symbols" && i + 1 < argc) {
            std::string syms = argv[++i];
            std::stringstream ss(syms);
            std::string item;
            config.symbols.clear();
            while (std::getline(ss, item, ',')) {
                config.symbols.push_back(item);
            }
        } else if (arg == "--duration" && i + 1 < argc) {
            config.duration_seconds = std::stoi(argv[++i]);
        } else if (arg == "--engine-version" && i + 1 < argc) {
            config.engine_version = argv[++i];
        } else if (arg == "--generator-version" && i + 1 < argc) {
            config.generator_version = argv[++i];
        } else if (arg == "--opens-at" && i + 1 < argc) {
            config.opens_at = argv[++i];
        } else if (arg == "--closes-at" && i + 1 < argc) {
            config.closes_at = argv[++i];
        } else if (arg == "--match-id" && i + 1 < argc) {
            config.match_id = argv[++i];
        } else if (arg == "--export" && i + 1 < argc) {
            config.export_path = argv[++i];
        } else if (arg == "--export-session" && i + 2 < argc) {
            config.export_session_id = argv[++i];
            config.export_path = argv[++i];
        }
    }

    if (config.is_active_session()) {
        for (const auto& sym : config.symbols) {
            config.symbol_universe.insert(sym);
        }
    }

    return config;
}

} // namespace Exchange
