#include "Core/Portfolio.hpp"
#include <algorithm>
#include <cmath>

namespace Exchange {

Portfolio::Portfolio(double initial_cash)
    : initial_cash_(initial_cash), cash_(initial_cash) {}

void Portfolio::reset(double cash) {
    initial_cash_ = cash;
    cash_ = cash;
    realized_pnl_ = 0.0;
    positions_.clear();
    cost_bases_.clear();
    market_prices_.clear();
}

void Portfolio::handleTrade(const Trade& trade, const std::string& target_client_id) {
    Side side;
    if (trade.buyer_client_id == target_client_id) {
        side = Side::Buy;
    } else if (trade.seller_client_id == target_client_id) {
        side = Side::Sell;
    } else {
        return; // Trade not involving the target portfolio
    }

    const std::string& symbol = trade.symbol;
    double price = priceToDouble(trade.price);
    Quantity qty = trade.qty;

    Quantity current_qty = positions_[symbol]; // Defaults to 0
    double current_basis = cost_bases_[symbol]; // Defaults to 0.0

    if (side == Side::Buy) {
        cash_ -= (qty * price);

        if (current_qty >= 0) {
            // Adding to a long position
            Quantity new_qty = current_qty + qty;
            if (new_qty > 0) {
                cost_bases_[symbol] = ((current_qty * current_basis) + (qty * price)) / new_qty;
            }
            positions_[symbol] = new_qty;
        } else {
            // Covering a short position
            Quantity close_qty = std::min(qty, -current_qty);

            // Short cover realized PnL = quantity * (entry_price - execution_price)
            realized_pnl_ += close_qty * (current_basis - price);

            Quantity new_qty = current_qty + qty;
            positions_[symbol] = new_qty;

            if (new_qty < 0) {
                // Still short, cost basis remains same
            } else if (new_qty > 0) {
                // Flipped to long position
                cost_bases_[symbol] = price;
            } else {
                // Flat
                cost_bases_[symbol] = 0.0;
            }
        }
    } else { // Side::Sell
        cash_ += (qty * price);

        if (current_qty <= 0) {
            // Adding to a short position
            Quantity new_qty = current_qty - qty; // e.g. -5 - 10 = -15
            if (new_qty < 0) {
                cost_bases_[symbol] = (((-current_qty) * current_basis) + (qty * price)) / (-new_qty);
            }
            positions_[symbol] = new_qty;
        } else {
            // Closing a long position
            Quantity close_qty = std::min(qty, current_qty);

            // Long sell realized PnL = quantity * (execution_price - entry_price)
            realized_pnl_ += close_qty * (price - current_basis);

            Quantity new_qty = current_qty - qty;
            positions_[symbol] = new_qty;

            if (new_qty > 0) {
                // Still long, cost basis remains same
            } else if (new_qty < 0) {
                // Flipped to short position
                cost_bases_[symbol] = price;
            } else {
                // Flat
                cost_bases_[symbol] = 0.0;
            }
        }
    }

    // Set market price to the trade execution price as a placeholder until updated by order book mid price
    if (market_prices_.find(symbol) == market_prices_.end()) {
        market_prices_[symbol] = price;
    }
}

void Portfolio::updateMarketPrice(const std::string& symbol, Price mid_price) {
    market_prices_[symbol] = priceToDouble(mid_price);
}

double Portfolio::getUnrealizedPnL() const {
    double total_unrealized = 0.0;
    for (const auto& [symbol, qty] : positions_) {
        if (qty == 0) continue;
        
        auto price_it = market_prices_.find(symbol);
        double market_price = (price_it != market_prices_.end()) ? price_it->second : 0.0;
        
        auto basis_it = cost_bases_.find(symbol);
        double basis = (basis_it != cost_bases_.end()) ? basis_it->second : 0.0;

        total_unrealized += qty * (market_price - basis);
    }
    return total_unrealized;
}

double Portfolio::getTotalValue() const {
    double value = cash_;
    for (const auto& [symbol, qty] : positions_) {
        if (qty == 0) continue;
        auto price_it = market_prices_.find(symbol);
        double market_price = (price_it != market_prices_.end()) ? price_it->second : 0.0;
        value += qty * market_price;
    }
    return value;
}

Quantity Portfolio::getPositionQty(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    return (it != positions_.end()) ? it->second : 0;
}

double Portfolio::getCostBasis(const std::string& symbol) const {
    auto it = cost_bases_.find(symbol);
    return (it != cost_bases_.end()) ? it->second : 0.0;
}

double Portfolio::getMarketPrice(const std::string& symbol) const {
    auto it = market_prices_.find(symbol);
    return (it != market_prices_.end()) ? it->second : 0.0;
}

std::vector<PositionDetails> Portfolio::getActivePositions() const {
    std::vector<PositionDetails> active;
    for (const auto& [symbol, qty] : positions_) {
        if (qty == 0) continue;
        double mkt_price = getMarketPrice(symbol);
        double basis = getCostBasis(symbol);
        double unrealized = qty * (mkt_price - basis);
        active.push_back({symbol, qty, basis, mkt_price, unrealized});
    }
    return active;
}

} // namespace Exchange
