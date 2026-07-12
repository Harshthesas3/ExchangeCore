#pragma once

#include "Core/Order.hpp"
#include "Core/Trade.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace Exchange {

struct PositionDetails {
    std::string symbol;
    Quantity qty;
    double cost_basis;
    double market_price;
    double unrealized_pnl;
};

class Portfolio {
public:
    explicit Portfolio(double initial_cash = 100000.0);

    // Updates portfolio based on a executed trade
    void handleTrade(const Trade& trade, const std::string& target_client_id);

    // Feeds latest market price for a symbol to compute unrealized PnL
    void updateMarketPrice(const std::string& symbol, Price mid_price);

    // Reset portfolio
    void reset(double cash = 100000.0);

    // Getters
    double getCash() const { return cash_; }
    double getInitialCash() const { return initial_cash_; }
    double getRealizedPnL() const { return realized_pnl_; }
    double getUnrealizedPnL() const;
    double getTotalValue() const; // Cash + Unrealized PnL

    Quantity getPositionQty(const std::string& symbol) const;
    double getCostBasis(const std::string& symbol) const;
    double getMarketPrice(const std::string& symbol) const;

    std::vector<PositionDetails> getActivePositions() const;

private:
    double initial_cash_;
    double cash_;
    double realized_pnl_{0.0};

    // Tracks positions: Symbol -> quantity (positive for long, negative for short)
    std::unordered_map<std::string, Quantity> positions_;

    // Tracks average entry cost basis: Symbol -> cost basis per share (as a double)
    std::unordered_map<std::string, double> cost_bases_;

    // Tracks current market mid prices: Symbol -> market price (as a double)
    std::unordered_map<std::string, double> market_prices_;
};

} // namespace Exchange
