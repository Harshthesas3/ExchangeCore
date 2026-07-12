#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace Exchange {

using Price = std::int64_t;      // Scaled by 10,000 (4 decimal places)
using Quantity = std::int64_t;   // Integer shares/units
using OrderID = std::uint64_t;

constexpr std::int64_t PRICE_SCALE = 10000;

inline Price doubleToPrice(double val) {
    return static_cast<Price>(val * PRICE_SCALE + (val >= 0 ? 0.5 : -0.5));
}

inline double priceToDouble(Price val) {
    return static_cast<double>(val) / PRICE_SCALE;
}

enum class Side : std::uint8_t {
    Buy,
    Sell
};

enum class OrderType : std::uint8_t {
    Limit,
    Market
};

enum class OrderStatus : std::uint8_t {
    New,
    Accepted,
    Rejected,
    PartiallyFilled,
    Filled,
    Cancelled,
    Modified
};

struct Order {
    OrderID id;
    std::string symbol;
    Side side;
    OrderType type;
    Price price;
    Quantity qty;
    Quantity filled_qty{0};
    OrderStatus status{OrderStatus::New};
    std::chrono::system_clock::time_point timestamp;
    std::string client_id; // Identifies user vs simulator bot

    Quantity remainingQty() const {
        return qty - filled_qty;
    }

    bool isFilled() const {
        return filled_qty >= qty;
    }
};

} // namespace Exchange
