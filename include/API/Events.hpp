#pragma once

#include "Core/Order.hpp"
#include "Core/Trade.hpp"
#include <string>
#include <vector>
#include <variant>
#include <utility>
#include <chrono>

namespace Exchange {

struct OrderAcceptedEvent {
    OrderID order_id;
    std::string symbol;
    Side side;
    Price price;
    Quantity qty;
    std::chrono::system_clock::time_point timestamp;
    std::string client_id;
};

struct OrderRejectedEvent {
    OrderID order_id;
    std::string symbol;
    std::string reason;
    std::chrono::system_clock::time_point timestamp;
    std::string client_id;
};

struct TradeExecutedEvent {
    Trade trade;
};

struct OrderCancelledEvent {
    OrderID order_id;
    std::string symbol;
    std::chrono::system_clock::time_point timestamp;
    std::string client_id;
};

struct OrderModifiedEvent {
    OrderID order_id;
    std::string symbol;
    Price new_price;
    Quantity new_qty;
    std::chrono::system_clock::time_point timestamp;
    std::string client_id;
};

struct MarketUpdatedEvent {
    std::string symbol;
    Price spread;
    Price mid_price;
    std::vector<std::pair<Price, Quantity>> bids; // Depth snapshot (consolidated levels)
    std::vector<std::pair<Price, Quantity>> asks;
};

struct StatisticsUpdatedEvent {
    uint64_t orders_received;
    uint64_t trades_count;
    uint64_t total_volume;
    double avg_latency_us;
    int64_t peak_latency_us;
    double orders_per_sec;
    double trades_per_sec;
};

using Event = std::variant<
    OrderAcceptedEvent,
    OrderRejectedEvent,
    TradeExecutedEvent,
    OrderCancelledEvent,
    OrderModifiedEvent,
    MarketUpdatedEvent,
    StatisticsUpdatedEvent
>;

class ExchangeEventListener {
public:
    virtual ~ExchangeEventListener() = default;
    virtual void onEvent(const Event& event) = 0;
};

} // namespace Exchange
