#include "Core/ExchangeEngine.hpp"
#include <chrono>
#include <algorithm>

namespace Exchange {

ExchangeEngine::ExchangeEngine() {
    matching_engine_ = std::make_unique<MatchingEngine>([this](const Trade& trade) {
        this->handleTradeExecution(trade);
    });
}

void ExchangeEngine::addSymbol(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    if (order_books_.find(symbol) == order_books_.end()) {
        order_books_.emplace(std::piecewise_construct,
                             std::forward_as_tuple(symbol),
                             std::forward_as_tuple(symbol));
    }
}

bool ExchangeEngine::hasSymbol(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    return order_books_.find(symbol) != order_books_.end();
}

std::vector<std::string> ExchangeEngine::getSymbols() const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    std::vector<std::string> symbols;
    symbols.reserve(order_books_.size());
    for (const auto& [symbol, _] : order_books_) {
        symbols.push_back(symbol);
    }
    return symbols;
}

void ExchangeEngine::submitOrder(const std::shared_ptr<Order>& order) {
    if (!order) return;

    auto start_time = std::chrono::steady_clock::now();
    stats_.recordOrderReceived();

    std::vector<Trade> trades;
    std::string symbol = order->symbol;
    
    {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        
        auto it = order_books_.find(symbol);
        if (it == order_books_.end()) {
            order->status = OrderStatus::Rejected;
            dispatchEvent(OrderRejectedEvent{
                order->id,
                order->symbol,
                "Unknown trading symbol",
                std::chrono::system_clock::now(),
                order->client_id
            });
            return;
        }

        OrderBook& book = it->second;
        
        // Dispatch acceptance event
        dispatchEvent(OrderAcceptedEvent{
            order->id,
            order->symbol,
            order->side,
            order->price,
            order->qty,
            order->timestamp,
            order->client_id
        });

        // Match order
        trades = matching_engine_->match(order, book);

        // Update stats and market data
        stats_.recordSpread(symbol, book.getSpread());
        portfolio_.updateMarketPrice(symbol, book.getMidPrice());

        // Dispatch book updates
        dispatchEvent(MarketUpdatedEvent{
            symbol,
            book.getSpread(),
            book.getMidPrice(),
            book.getBidDepth(10),
            book.getAskDepth(10)
        });
    }

    // Publish Stats Update
    auto stats_snap = stats_.getSnapshot();
    dispatchEvent(StatisticsUpdatedEvent{
        stats_snap.orders_received,
        stats_snap.trades_count,
        stats_snap.total_volume,
        stats_snap.avg_latency_us,
        stats_snap.peak_latency_us,
        stats_snap.orders_per_sec,
        stats_snap.trades_per_sec
    });

    auto end_time = std::chrono::steady_clock::now();
    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
    stats_.recordLatency(latency);
}

bool ExchangeEngine::cancelOrder(OrderID id, const std::string& symbol) {
    auto start_time = std::chrono::steady_clock::now();
    bool success = false;

    {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        auto it = order_books_.find(symbol);
        if (it != order_books_.end()) {
            OrderBook& book = it->second;
            auto order = book.find(id);
            if (order) {
                std::string client_id = order->client_id;
                success = matching_engine_->cancel(id, book);
                if (success) {
                    dispatchEvent(OrderCancelledEvent{
                        id,
                        symbol,
                        std::chrono::system_clock::now(),
                        client_id
                    });

                    // Update stats and market data
                    stats_.recordSpread(symbol, book.getSpread());
                    portfolio_.updateMarketPrice(symbol, book.getMidPrice());

                    dispatchEvent(MarketUpdatedEvent{
                        symbol,
                        book.getSpread(),
                        book.getMidPrice(),
                        book.getBidDepth(10),
                        book.getAskDepth(10)
                    });
                }
            }
        }
    }

    if (success) {
        auto stats_snap = stats_.getSnapshot();
        dispatchEvent(StatisticsUpdatedEvent{
            stats_snap.orders_received,
            stats_snap.trades_count,
            stats_snap.total_volume,
            stats_snap.avg_latency_us,
            stats_snap.peak_latency_us,
            stats_snap.orders_per_sec,
            stats_snap.trades_per_sec
        });

        auto end_time = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        stats_.recordLatency(latency);
    }

    return success;
}

bool ExchangeEngine::modifyOrder(OrderID id, const std::string& symbol, Price new_price, Quantity new_qty, OrderID new_order_id) {
    auto start_time = std::chrono::steady_clock::now();
    bool success = false;
    std::vector<Trade> trades;

    {
        std::lock_guard<std::mutex> lock(engine_mutex_);
        auto it = order_books_.find(symbol);
        if (it != order_books_.end()) {
            OrderBook& book = it->second;
            auto order = book.find(id);
            if (order) {
                std::string client_id = order->client_id;
                success = matching_engine_->modify(id, new_price, new_qty, book, new_order_id, trades);
                if (success) {
                    dispatchEvent(OrderModifiedEvent{
                        id,
                        symbol,
                        new_price,
                        new_qty,
                        std::chrono::system_clock::now(),
                        client_id
                    });

                    stats_.recordSpread(symbol, book.getSpread());
                    portfolio_.updateMarketPrice(symbol, book.getMidPrice());

                    dispatchEvent(MarketUpdatedEvent{
                        symbol,
                        book.getSpread(),
                        book.getMidPrice(),
                        book.getBidDepth(10),
                        book.getAskDepth(10)
                    });
                }
            }
        }
    }

    if (success) {
        auto stats_snap = stats_.getSnapshot();
        dispatchEvent(StatisticsUpdatedEvent{
            stats_snap.orders_received,
            stats_snap.trades_count,
            stats_snap.total_volume,
            stats_snap.avg_latency_us,
            stats_snap.peak_latency_us,
            stats_snap.orders_per_sec,
            stats_snap.trades_per_sec
        });

        auto end_time = std::chrono::steady_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count();
        stats_.recordLatency(latency);
    }

    return success;
}

std::vector<std::pair<Price, Quantity>> ExchangeEngine::getBidDepthSnapshot(const std::string& symbol, size_t depth) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    auto it = order_books_.find(symbol);
    if (it != order_books_.end()) {
        return it->second.getBidDepth(depth);
    }
    return {};
}

std::vector<std::pair<Price, Quantity>> ExchangeEngine::getAskDepthSnapshot(const std::string& symbol, size_t depth) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    auto it = order_books_.find(symbol);
    if (it != order_books_.end()) {
        return it->second.getAskDepth(depth);
    }
    return {};
}

Price ExchangeEngine::getSpread(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    auto it = order_books_.find(symbol);
    return (it != order_books_.end()) ? it->second.getSpread() : 0;
}

Price ExchangeEngine::getMidPrice(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    auto it = order_books_.find(symbol);
    return (it != order_books_.end()) ? it->second.getMidPrice() : 0;
}

double ExchangeEngine::getVWAP(const std::string& symbol) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    return stats_.getVWAP(symbol);
}

StatisticsSnapshot ExchangeEngine::getStatistics() const {
    return stats_.getSnapshot();
}

Portfolio ExchangeEngine::getPortfolio() const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    return portfolio_;
}

std::vector<Trade> ExchangeEngine::getRecentTrades(size_t limit) const {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    std::vector<Trade> result;
    size_t count = std::min(limit, recent_trades_.size());
    result.reserve(count);
    
    // Return newest trades first
    auto it = recent_trades_.rbegin();
    for (size_t i = 0; i < count; ++i, ++it) {
        result.push_back(*it);
    }
    return result;
}

void ExchangeEngine::registerListener(std::shared_ptr<ExchangeEventListener> listener) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    listeners_.push_back(std::move(listener));
}

void ExchangeEngine::unregisterListener(const std::shared_ptr<ExchangeEventListener>& listener) {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    listeners_.erase(std::remove(listeners_.begin(), listeners_.end(), listener), listeners_.end());
}

void ExchangeEngine::reset() {
    std::lock_guard<std::mutex> lock(engine_mutex_);
    order_books_.clear();
    stats_.reset();
    portfolio_.reset();
    recent_trades_.clear();
}

void ExchangeEngine::dispatchEvent(const Event& event) {
    // Expecting to be called under engine_mutex_
    for (const auto& listener : listeners_) {
        if (listener) {
            listener->onEvent(event);
        }
    }
}

void ExchangeEngine::handleTradeExecution(const Trade& trade) {
    // Add to recent trades log
    recent_trades_.push_back(trade);
    if (recent_trades_.size() > max_recent_trades_) {
        recent_trades_.pop_front();
    }

    // Record to statistics
    stats_.recordTrade(trade);

    // Record trade to portfolio (User portfolio checks for "USER" clientId involvement)
    portfolio_.handleTrade(trade, "USER");

    // Dispatch event to listeners
    dispatchEvent(TradeExecutedEvent{trade});
}

} // namespace Exchange
