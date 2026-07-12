#include "Core/OrderBook.hpp"

namespace Exchange {

OrderBook::OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

void OrderBook::insert(const std::shared_ptr<Order>& order) {
    if (!order) return;

    if (order->status == OrderStatus::New) {
        order->status = OrderStatus::Accepted;
    }

    if (order->side == Side::Buy) {
        auto& price_list = bids_[order->price];
        price_list.push_back(order);
        order_lookup_[order->id] = OrderBookPosition{
            Side::Buy,
            order->price,
            std::prev(price_list.end())
        };
    } else {
        auto& price_list = asks_[order->price];
        price_list.push_back(order);
        order_lookup_[order->id] = OrderBookPosition{
            Side::Sell,
            order->price,
            std::prev(price_list.end())
        };
    }
}

bool OrderBook::erase(OrderID id) {
    auto lookup_it = order_lookup_.find(id);
    if (lookup_it == order_lookup_.end()) {
        return false;
    }

    const auto& pos = lookup_it->second;
    if (pos.side == Side::Buy) {
        auto map_it = bids_.find(pos.price);
        if (map_it != bids_.end()) {
            map_it->second.erase(pos.it);
            if (map_it->second.empty()) {
                bids_.erase(map_it);
            }
        }
    } else {
        auto map_it = asks_.find(pos.price);
        if (map_it != asks_.end()) {
            map_it->second.erase(pos.it);
            if (map_it->second.empty()) {
                asks_.erase(map_it);
            }
        }
    }

    order_lookup_.erase(lookup_it);
    return true;
}

std::shared_ptr<Order> OrderBook::find(OrderID id) const {
    auto lookup_it = order_lookup_.find(id);
    if (lookup_it == order_lookup_.end()) {
        return nullptr;
    }
    return *lookup_it->second.it;
}

std::shared_ptr<Order> OrderBook::getBestBid() const {
    if (bids_.empty() || bids_.begin()->second.empty()) {
        return nullptr;
    }
    return bids_.begin()->second.front();
}

void OrderBook::popBestBid() {
    if (bids_.empty()) return;
    auto it = bids_.begin();
    auto& list = it->second;
    if (!list.empty()) {
        order_lookup_.erase(list.front()->id);
        list.pop_front();
    }
    if (list.empty()) {
        bids_.erase(it);
    }
}

std::shared_ptr<Order> OrderBook::getBestAsk() const {
    if (asks_.empty() || asks_.begin()->second.empty()) {
        return nullptr;
    }
    return asks_.begin()->second.front();
}

void OrderBook::popBestAsk() {
    if (asks_.empty()) return;
    auto it = asks_.begin();
    auto& list = it->second;
    if (!list.empty()) {
        order_lookup_.erase(list.front()->id);
        list.pop_front();
    }
    if (list.empty()) {
        asks_.erase(it);
    }
}

Price OrderBook::getSpread() const {
    if (bids_.empty() || asks_.empty()) {
        return 0;
    }
    return asks_.begin()->first - bids_.begin()->first;
}

Price OrderBook::getMidPrice() const {
    if (!bids_.empty() && !asks_.empty()) {
        return (bids_.begin()->first + asks_.begin()->first) / 2;
    } else if (!bids_.empty()) {
        return bids_.begin()->first;
    } else if (!asks_.empty()) {
        return asks_.begin()->first;
    }
    return 0;
}

std::vector<std::pair<Price, Quantity>> OrderBook::getBidDepth(size_t max_depth) const {
    std::vector<std::pair<Price, Quantity>> depth;
    depth.reserve(max_depth);

    size_t count = 0;
    for (const auto& [price, orders] : bids_) {
        if (count >= max_depth) break;
        Quantity total_qty = 0;
        for (const auto& order : orders) {
            total_qty += order->remainingQty();
        }
        depth.emplace_back(price, total_qty);
        count++;
    }
    return depth;
}

std::vector<std::pair<Price, Quantity>> OrderBook::getAskDepth(size_t max_depth) const {
    std::vector<std::pair<Price, Quantity>> depth;
    depth.reserve(max_depth);

    size_t count = 0;
    for (const auto& [price, orders] : asks_) {
        if (count >= max_depth) break;
        Quantity total_qty = 0;
        for (const auto& order : orders) {
            total_qty += order->remainingQty();
        }
        depth.emplace_back(price, total_qty);
        count++;
    }
    return depth;
}

} // namespace Exchange
