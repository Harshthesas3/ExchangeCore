#include "Core/ExchangeEngine.hpp"
#include "Core/MarketSimulator.hpp"
#include "Core/SessionManager.hpp"
#include "Core/QuantArenaExporter.hpp"
#include "TUI/TerminalApp.hpp"
#include <memory>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

void printHelp() {
    std::cout << "ExchangeCore Simulator\n"
              << "Options:\n"
              << "  --headless                Run headless (no UI)\n"
              << "  --seed <hex>              QuantArena match seed\n"
              << "  --match-id <id>           QuantArena match ID\n"
              << "  --duration-sec <N>        Duration of headless simulation in simulated seconds (default 300)\n"
              << "  --config <file.json>      Path to JSON config (fallback)\n";
}

int main(int argc, char* argv[]) {
    bool headless = false;
    std::string seed = "";
    std::string match_id = "default_match";
    std::string config_file = "";
    int duration_sec = 300;

    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--headless") {
            headless = true;
        } else if (args[i] == "--seed" && i + 1 < args.size()) {
            seed = args[++i];
        } else if (args[i] == "--match-id" && i + 1 < args.size()) {
            match_id = args[++i];
        } else if (args[i] == "--duration-sec" && i + 1 < args.size()) {
            duration_sec = std::stoi(args[++i]);
        } else if (args[i] == "--config" && i + 1 < args.size()) {
            config_file = args[++i];
        } else if (args[i] == "--help" || args[i] == "-h") {
            printHelp();
            return 0;
        }
    }

    if (headless || !seed.empty() || !config_file.empty()) {
        std::cout << "Starting headless QuantArena Match Simulation...\n";
        
        Exchange::SessionConfig config;
        // Basic config fallback (could be expanded with real JSON parsing if nlohmann/json was available)
        if (!seed.empty()) {
            config.seed_hex = seed;
            if (config.seed_hex.starts_with("0x") || config.seed_hex.starts_with("0X")) {
                config.seed_hex = config.seed_hex.substr(2);
            }
        }
        config.match_id = match_id;
        
        auto session_mgr = std::make_shared<Exchange::SessionManager>(config);
        auto engine = std::make_shared<Exchange::ExchangeEngine>(session_mgr);
        auto simulator = std::make_shared<Exchange::MarketSimulator>(*engine, session_mgr);
        auto exporter = std::make_shared<Exchange::QuantArenaExporter>(session_mgr, "./quantarena_exports/" + config.match_id);
        
        engine->registerListener(exporter);

        simulator->start();
        
        // Wall-clock timeout for headless mode
        auto wall_end = std::chrono::steady_clock::now() + std::chrono::seconds(duration_sec);
        while (std::chrono::steady_clock::now() < wall_end) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        simulator->stop();
        exporter->finishExport();
        std::cout << "Match simulation complete. Log exported to ./quantarena_exports/" << config.match_id << "\n";
    } else {
        // 1. Create exchange engine coordinator
        auto engine = std::make_shared<Exchange::ExchangeEngine>();

        // 2. Create and start background market simulation bots
        auto simulator = std::make_shared<Exchange::MarketSimulator>(*engine);
        simulator->start();

        // 3. Create and launch Terminal TUI App
        auto app = std::make_shared<Exchange::TUI::TerminalApp>(engine, simulator);
        app->run();

        // 4. Ensure background simulator stops on exit
        simulator->stop();
    }

    return 0;
}
