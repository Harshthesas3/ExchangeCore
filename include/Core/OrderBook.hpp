#pragma once

#include "Core/Order.hpp"
#include <string>
#include <map>
#include <list>
#include <unordered_map>
#include <memory>
#include <vector>
#include <utility>

namespace Exchange {

struct OrderBookPosition {
    Side side;
    Price price;
    std::list<std::shared_ptr<Order>>::iterator it;
};

class OrderBook {
public:
    explicit OrderBook(std::string symbol);

    const std::string& getSymbol() const { return symbol_; }

    // Inserts order directly into the book (time priority at price level)
    void insert(const std::shared_ptr<Order>& order);

    // Removes an order from the book by ID (returns true if successfully erased)
    bool erase(OrderID id);

    // Finds an order by ID (returns nullptr if not found)
    std::shared_ptr<Order> find(OrderID id) const;

    // Get read-only references to the bids and asks maps
    const std::map<Price, std::list<std::shared_ptr<Order>>, std::greater<Price>>& getBids() const { return bids_; }
    const std::map<Price, std::list<std::shared_ptr<Order>>, std::less<Price>>& getAsks() const { return asks_; }

    // Safe accessors/mutators for best bid/ask (used by Matching Engine)
    std::shared_ptr<Order> getBestBid() const;
    void popBestBid();
    std::shared_ptr<Order> getBestAsk() const;
    void popBestAsk();

    // Helper statistics
    Price getSpread() const;
    Price getMidPrice() const;

    // Returns a consolidated list of (Price, Cumulative Quantity) up to a max depth
    std::vector<std::pair<Price, Quantity>> getBidDepth(size_t max_depth) const;
    std::vector<std::pair<Price, Quantity>> getAskDepth(size_t max_depth) const;

    bool empty() const { return bids_.empty() && asks_.empty(); }

private:
    std::string symbol_;
    std::map<Price, std::list<std::shared_ptr<Order>>, std::greater<Price>> bids_;
    std::map<Price, std::list<std::shared_ptr<Order>>, std::less<Price>> asks_;
    std::unordered_map<OrderID, OrderBookPosition> order_lookup_;
};

} // namespace Exchange
