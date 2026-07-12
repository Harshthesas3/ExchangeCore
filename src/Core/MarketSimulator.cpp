#include "Core/MarketSimulator.hpp"
#include <chrono>
#include <algorithm>

namespace Exchange {

MarketSimulator::MarketSimulator(ExchangeEngine& engine)
    : engine_(engine) {
    ref_prices_["AAPL"] = 150.0;
    ref_prices_["MSFT"] = 300.0;
    ref_prices_["GOOG"] = 100.0;
    ref_prices_["BTCUSD"] = 60000.0;
    ref_prices_["ETHUSD"] = 3000.0;
}

MarketSimulator::~MarketSimulator() {
    stop();
}

void MarketSimulator::start() {
    if (running_) return;
    running_ = true;
    worker_thread_ = std::thread(&MarketSimulator::run, this);
}

void MarketSimulator::stop() {
    if (!running_) return;
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void MarketSimulator::run() {
    std::vector<std::string> symbols = {"AAPL", "MSFT", "GOOG", "BTCUSD", "ETHUSD"};
    
    // Ensure all symbols exist in the engine
    for (const auto& symbol : symbols) {
        engine_.addSymbol(symbol);
    }

    std::uniform_int_distribution<size_t> sym_dist(0, symbols.size() - 1);
    std::uniform_real_distribution<double> action_dist(0.0, 1.0);
    std::uniform_int_distribution<int> qty_dist(5, 50);

    while (running_) {
        // Sleep for a random short duration to throttle simulator activity
        std::this_thread::sleep_for(std::chrono::milliseconds(10 + (rng_() % 40)));

        std::string symbol = symbols[sym_dist(rng_)];
        double ref_price = ref_prices_[symbol];

        // Random walk step (-0.05% to +0.05%)
        double price_change_pct = std::uniform_real_distribution<double>(-0.0005, 0.0005)(rng_);
        ref_price *= (1.0 + price_change_pct);
        ref_prices_[symbol] = ref_price;

        double action = action_dist(rng_);

        if (action < 0.10) {
            // Cancel oldest active bot order
            auto& q = bot_orders_[symbol];
            if (!q.empty()) {
                OrderID cancel_id = q.front();
                q.pop_front();
                engine_.cancelOrder(cancel_id, symbol);
            }
        } else if (action < 0.25) {
            // Submit market order (forces trades)
            Side side = (rng_() % 2 == 0) ? Side::Buy : Side::Sell;
            Quantity qty = qty_dist(rng_);

            auto order = std::make_shared<Order>();
            order->id = next_bot_order_id_++;
            order->symbol = symbol;
            order->side = side;
            order->type = OrderType::Market;
            order->price = 0; // Price ignored for market orders
            order->qty = qty;
            order->timestamp = std::chrono::system_clock::now();
            order->client_id = "BOT_" + std::to_string((rng_() % 5) + 1);

            engine_.submitOrder(order);
        } else {
            // Submit limit order
            Side side = (rng_() % 2 == 0) ? Side::Buy : Side::Sell;
            Quantity qty = qty_dist(rng_);
            Price price = 0;

            if (side == Side::Buy) {
                double discount = std::uniform_real_distribution<double>(0.0001, 0.0015)(rng_);
                price = doubleToPrice(ref_price * (1.0 - discount));
            } else {
                double premium = std::uniform_real_distribution<double>(0.0001, 0.0015)(rng_);
                price = doubleToPrice(ref_price * (1.0 + premium));
            }

            if (price <= 0) price = 10000; // Default floor $1.00

            auto order = std::make_shared<Order>();
            order->id = next_bot_order_id_++;
            order->symbol = symbol;
            order->side = side;
            order->type = OrderType::Limit;
            order->price = price;
            order->qty = qty;
            order->timestamp = std::chrono::system_clock::now();
            order->client_id = "BOT_" + std::to_string((rng_() % 5) + 1);

            engine_.submitOrder(order);

            auto& q = bot_orders_[symbol];
            q.push_back(order->id);

            // Quote replacement: cancel oldest order when queue limit is reached
            if (q.size() > max_bot_orders_per_symbol_) {
                OrderID cancel_id = q.front();
                q.pop_front();
                engine_.cancelOrder(cancel_id, symbol);
            }
        }
    }
}

} // namespace Exchange
