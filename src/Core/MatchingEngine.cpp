#include "Core/MatchingEngine.hpp"
#include <algorithm>

namespace Exchange {

MatchingEngine::MatchingEngine(TradeCallback trade_cb) : trade_cb_(std::move(trade_cb)) {}

std::vector<Trade> MatchingEngine::match(const std::shared_ptr<Order>& order, OrderBook& book) {
    std::vector<Trade> trades;
    if (!order || order->qty <= 0) return trades;

    if (order->side == Side::Buy) {
        // Match against best asks
        while (order->remainingQty() > 0) {
            auto best_ask = book.getBestAsk();
            if (!best_ask) break;

            // Check if price limit is respected for Limit orders
            if (order->type == OrderType::Limit && order->price < best_ask->price) {
                break;
            }

            // Execute trade
            Quantity match_qty = std::min(order->remainingQty(), best_ask->remainingQty());
            order->filled_qty += match_qty;
            best_ask->filled_qty += match_qty;

            Trade trade{
                next_trade_id_++,
                order->id,
                best_ask->id,
                order->symbol,
                best_ask->price, // price is determined by the resting order (best_ask)
                match_qty,
                std::chrono::system_clock::now(), // Will be updated in ExchangeEngine
                order->client_id,
                best_ask->client_id,
                order->side,
                order->type
            };

            trades.push_back(trade);
            if (trade_cb_) {
                trade_cb_(trade);
            }

            if (best_ask->isFilled()) {
                best_ask->status = OrderStatus::Filled;
                book.popBestAsk();
            } else {
                best_ask->status = OrderStatus::PartiallyFilled;
            }
        }

        if (order->isFilled()) {
            order->status = OrderStatus::Filled;
        } else {
            order->status = order->filled_qty > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Accepted;
            if (order->type == OrderType::Limit) {
                book.insert(order);
            } else {
                // Market order unfilled portion is cancelled
                order->status = OrderStatus::Cancelled;
            }
        }
    } else {
        // Match Sell order against best bids
        while (order->remainingQty() > 0) {
            auto best_bid = book.getBestBid();
            if (!best_bid) break;

            // Check price limit for Limit orders
            if (order->type == OrderType::Limit && order->price > best_bid->price) {
                break;
            }

            // Execute trade
            Quantity match_qty = std::min(order->remainingQty(), best_bid->remainingQty());
            order->filled_qty += match_qty;
            best_bid->filled_qty += match_qty;

            Trade trade{
                next_trade_id_++,
                best_bid->id,
                order->id,
                order->symbol,
                best_bid->price, // price is determined by the resting order (best_bid)
                match_qty,
                std::chrono::system_clock::now(), // Will be updated in ExchangeEngine
                best_bid->client_id,
                order->client_id,
                order->side,
                order->type
            };

            trades.push_back(trade);
            if (trade_cb_) {
                trade_cb_(trade);
            }

            if (best_bid->isFilled()) {
                best_bid->status = OrderStatus::Filled;
                book.popBestBid();
            } else {
                best_bid->status = OrderStatus::PartiallyFilled;
            }
        }

        if (order->isFilled()) {
            order->status = OrderStatus::Filled;
        } else {
            order->status = order->filled_qty > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Accepted;
            if (order->type == OrderType::Limit) {
                book.insert(order);
            } else {
                // Market order unfilled portion is cancelled
                order->status = OrderStatus::Cancelled;
            }
        }
    }

    return trades;
}

bool MatchingEngine::cancel(OrderID id, OrderBook& book) {
    auto order = book.find(id);
    if (!order) return false;

    if (book.erase(id)) {
        order->status = OrderStatus::Cancelled;
        return true;
    }
    return false;
}

bool MatchingEngine::modify(OrderID id, Price new_price, Quantity new_qty, OrderBook& book, OrderID new_order_id, std::vector<Trade>& out_trades) {
    auto order = book.find(id);
    if (!order) return false;

    // Check quantity validity: cannot modify to less than what is already filled
    if (new_qty < order->filled_qty) {
        return false;
    }

    // Check if priority is retained: same price, smaller or equal quantity
    if (new_price == order->price && new_qty <= order->qty) {
        order->qty = new_qty;
        order->status = OrderStatus::Modified;
        return true;
    }

    // Priority is lost: cancel and re-submit
    auto client_id = order->client_id;
    auto symbol = order->symbol;
    auto side = order->side;
    auto type = order->type;
    Quantity remaining_to_fill = new_qty - order->filled_qty;

    if (!book.erase(id)) {
        return false;
    }
    order->status = OrderStatus::Cancelled;

    if (remaining_to_fill <= 0) {
        return true;
    }

    // Re-submit remaining quantity as a new order
    auto new_order = std::make_shared<Order>();
    new_order->id = new_order_id;
    new_order->symbol = std::move(symbol);
    new_order->side = side;
    new_order->type = type;
    new_order->price = new_price;
    new_order->qty = remaining_to_fill;
    new_order->filled_qty = 0;
    new_order->status = OrderStatus::New;
    new_order->timestamp = std::chrono::system_clock::now();
    new_order->client_id = std::move(client_id);

    out_trades = match(new_order, book);
    return true;
}

} // namespace Exchange
