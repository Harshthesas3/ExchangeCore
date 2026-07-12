#include "Core/ExchangeEngine.hpp"
#include "Core/MarketSimulator.hpp"
#include "TUI/TerminalApp.hpp"
#include <memory>

int main() {
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

    return 0;
}
