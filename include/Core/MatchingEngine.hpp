#pragma once

#include "Core/Order.hpp"
#include "Core/Trade.hpp"
#include "Core/OrderBook.hpp"
#include <vector>
#include <memory>
#include <functional>

namespace Exchange {

class MatchingEngine {
public:
    using TradeCallback = std::function<void(const Trade&)>;

    explicit MatchingEngine(TradeCallback trade_cb);

    // Matches a new incoming order against the OrderBook.
    // For Limit Orders, any unfilled quantity is inserted into the OrderBook.
    // For Market Orders, any unfilled quantity is cancelled immediately.
    // Returns the list of trades generated.
    std::vector<Trade> match(const std::shared_ptr<Order>& order, OrderBook& book);

    // Cancels an order. Returns true if found and cancelled.
    bool cancel(OrderID id, OrderBook& book);

    // Modifies an order's price and/or quantity.
    // Returns true if modified successfully.
    // Triggers matching for the new size/price if priority is lost, writing trades to out_trades.
    bool modify(OrderID id, Price new_price, Quantity new_qty, OrderBook& book, OrderID new_order_id, std::vector<Trade>& out_trades);

private:
    TradeCallback trade_cb_;
    OrderID next_trade_id_{1};
};

} // namespace Exchange
