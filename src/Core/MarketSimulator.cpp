#include "Core/MarketSimulator.hpp"
#include "Core/SessionManager.hpp"
#include <chrono>
#include <algorithm>

namespace Exchange {

MarketSimulator::MarketSimulator(ExchangeEngine& engine, std::shared_ptr<SessionManager> session_mgr)
    : engine_(engine), session_mgr_(session_mgr) {
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

    auto get_rand = [&]() -> uint64_t {
        if (session_mgr_ && session_mgr_->isMatchMode()) {
            return session_mgr_->nextRandom();
        }
        return rng_();
    };

    auto get_now = [&]() -> std::chrono::system_clock::time_point {
        if (session_mgr_) {
            return session_mgr_->now();
        }
        return std::chrono::system_clock::now();
    };

    while (running_) {
        // Sleep for a random short duration to throttle simulator activity
        if (!session_mgr_ || !session_mgr_->isMatchMode()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10 + (get_rand() % 40)));
        } else {
            // In match mode, sleep briefly to let other threads (like the TUI rendering thread) run,
            // but keep the latency extremely low (e.g. 1ms) as requested.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        std::string symbol = symbols[sym_dist(rng_)]; // We'll manually map this to get_rand() for determinism
        symbol = symbols[get_rand() % symbols.size()];
        double ref_price = ref_prices_[symbol];

        // Random walk step (-0.05% to +0.05%)
        double price_change_pct = -0.0005 + (get_rand() % 10000) / 10000000.0;
        ref_price *= (1.0 + price_change_pct);
        ref_prices_[symbol] = ref_price;

        double action = (get_rand() % 100) / 100.0;

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
            Side side = (get_rand() % 2 == 0) ? Side::Buy : Side::Sell;
            Quantity qty = 5 + (get_rand() % 46);

            auto order = std::make_shared<Order>();
            order->id = next_bot_order_id_++;
            order->symbol = symbol;
            order->side = side;
            order->type = OrderType::Market;
            order->price = 0; // Price ignored for market orders
            order->qty = qty;
            order->timestamp = get_now();
            order->client_id = "BOT_" + std::to_string((get_rand() % 5) + 1);

            engine_.submitOrder(order);
        } else {
            // Submit limit order
            Side side = (get_rand() % 2 == 0) ? Side::Buy : Side::Sell;
            Quantity qty = 5 + (get_rand() % 46);
            Price price = 0;

            if (side == Side::Buy) {
                double discount = 0.0001 + (get_rand() % 140) / 100000.0;
                price = doubleToPrice(ref_price * (1.0 - discount));
            } else {
                double premium = 0.0001 + (get_rand() % 140) / 100000.0;
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
            order->timestamp = get_now();
            order->client_id = "BOT_" + std::to_string((get_rand() % 5) + 1);

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
