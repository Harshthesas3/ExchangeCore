#pragma once

#include "Core/Order.hpp"
#include <string>
#include <chrono>

namespace Exchange {

struct Trade {
    OrderID trade_id;
    OrderID buy_order_id;
    OrderID sell_order_id;
    std::string symbol;
    Price price;
    Quantity qty;
    std::chrono::system_clock::time_point timestamp;
    std::string buyer_client_id;
    std::string seller_client_id;
    Side taker_side;
    OrderType taker_type;
};

} // namespace Exchange
