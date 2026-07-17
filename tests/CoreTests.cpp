#include <gtest/gtest.h>
#include "Core/Order.hpp"
#include "Core/Trade.hpp"
#include "Core/OrderBook.hpp"
#include "Core/MatchingEngine.hpp"
#include "Core/Portfolio.hpp"
#include "Core/Statistics.hpp"
#include "Core/ExchangeEngine.hpp"

#include <memory>
#include <vector>

using namespace Exchange;

// Test helper to create orders
std::shared_ptr<Order> createOrder(OrderID id, std::string symbol, Side side, OrderType type, Price price, Quantity qty, std::string client_id = "USER") {
    auto o = std::make_shared<Order>();
    o->id = id;
    o->symbol = std::move(symbol);
    o->side = side;
    o->type = type;
    o->price = price;
    o->qty = qty;
    o->filled_qty = 0;
    o->status = OrderStatus::New;
    o->timestamp = std::chrono::system_clock::now();
    o->client_id = std::move(client_id);
    return o;
}

// --- OrderBook Tests ---
TEST(OrderBookTest, BasicInsertAndErase) {
    OrderBook book("AAPL");
    EXPECT_EQ(book.getSymbol(), "AAPL");
    EXPECT_TRUE(book.empty());

    auto o1 = createOrder(1, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10);
    auto o2 = createOrder(2, "AAPL", Side::Sell, OrderType::Limit, doubleToPrice(151.0), 20);

    book.insert(o1);
    book.insert(o2);

    EXPECT_FALSE(book.empty());
    EXPECT_EQ(book.getSpread(), doubleToPrice(1.0));
    EXPECT_EQ(book.getMidPrice(), doubleToPrice(150.5));

    // Verify lookup
    auto found = book.find(1);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 1);
    EXPECT_EQ(found->qty, 10);

    // Verify erase
    EXPECT_TRUE(book.erase(1));
    EXPECT_EQ(book.find(1), nullptr);
    EXPECT_EQ(book.getSpread(), 0); // One side empty
}

TEST(OrderBookTest, DepthCalculation) {
    OrderBook book("MSFT");
    book.insert(createOrder(1, "MSFT", Side::Buy, OrderType::Limit, doubleToPrice(300.0), 10));
    book.insert(createOrder(2, "MSFT", Side::Buy, OrderType::Limit, doubleToPrice(300.0), 5));
    book.insert(createOrder(3, "MSFT", Side::Buy, OrderType::Limit, doubleToPrice(299.0), 15));

    auto depth = book.getBidDepth(2);
    ASSERT_EQ(depth.size(), 2);
    EXPECT_EQ(depth[0].first, doubleToPrice(300.0));
    EXPECT_EQ(depth[0].second, 15); // 10 + 5 consolidated
    EXPECT_EQ(depth[1].first, doubleToPrice(299.0));
    EXPECT_EQ(depth[1].second, 15);
}

// --- Matching Engine Tests ---
TEST(MatchingEngineTest, LimitOrderFullFill) {
    std::vector<Trade> recorded_trades;
    MatchingEngine engine([&](const Trade& t) { recorded_trades.push_back(t); });

    OrderBook book("AAPL");
    // Insert resting Sell order
    auto sell = createOrder(1, "AAPL", Side::Sell, OrderType::Limit, doubleToPrice(150.0), 10, "BOT_1");
    book.insert(sell);

    // Incoming Buy order
    auto buy = createOrder(2, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10, "USER");
    auto trades = engine.match(buy, book);

    EXPECT_EQ(trades.size(), 1);
    EXPECT_EQ(recorded_trades.size(), 1);
    EXPECT_EQ(buy->status, OrderStatus::Filled);
    EXPECT_EQ(sell->status, OrderStatus::Filled);
    EXPECT_TRUE(book.empty());

    EXPECT_EQ(trades[0].price, doubleToPrice(150.0));
    EXPECT_EQ(trades[0].qty, 10);
    EXPECT_EQ(trades[0].buyer_client_id, "USER");
    EXPECT_EQ(trades[0].seller_client_id, "BOT_1");
}

TEST(MatchingEngineTest, FIFOPriority) {
    MatchingEngine engine(nullptr);
    OrderBook book("AAPL");

    // Two resting Buy orders at 150.0
    auto buy1 = createOrder(1, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10, "USER_1");
    auto buy2 = createOrder(2, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10, "USER_2");
    book.insert(buy1);
    book.insert(buy2);

    // Sell order that matches only 12 shares
    auto sell = createOrder(3, "AAPL", Side::Sell, OrderType::Limit, doubleToPrice(150.0), 12, "BOT_1");
    auto trades = engine.match(sell, book);

    // FIFO: buy1 should be completely filled, buy2 partially filled (2 shares)
    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(buy1->status, OrderStatus::Filled);
    EXPECT_EQ(buy2->status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(buy2->filled_qty, 2);
    EXPECT_EQ(sell->status, OrderStatus::Filled);
}

TEST(MatchingEngineTest, PartialFillsAndResting) {
    MatchingEngine engine(nullptr);
    OrderBook book("AAPL");

    // Buy order for 20 shares
    auto buy = createOrder(1, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 20, "USER");
    auto trades1 = engine.match(buy, book);
    EXPECT_TRUE(trades1.empty()); // No resting asks

    // Sell order for 15 shares
    auto sell = createOrder(2, "AAPL", Side::Sell, OrderType::Limit, doubleToPrice(150.0), 15, "BOT");
    auto trades2 = engine.match(sell, book);

    EXPECT_EQ(trades2.size(), 1);
    EXPECT_EQ(trades2[0].qty, 15);
    EXPECT_EQ(buy->filled_qty, 15);
    EXPECT_EQ(buy->status, OrderStatus::PartiallyFilled);
    EXPECT_EQ(sell->status, OrderStatus::Filled);

    // Verify buy order is still in the book with remaining qty 5
    auto bid_depth = book.getBidDepth(1);
    ASSERT_EQ(bid_depth.size(), 1);
    EXPECT_EQ(bid_depth[0].second, 5);
}

TEST(MatchingEngineTest, MarketOrderBehavior) {
    MatchingEngine engine(nullptr);
    OrderBook book("AAPL");

    // Resting limit asks
    book.insert(createOrder(1, "AAPL", Side::Sell, OrderType::Limit, doubleToPrice(150.0), 10, "BOT1"));
    book.insert(createOrder(2, "AAPL", Side::Sell, OrderType::Limit, doubleToPrice(151.0), 10, "BOT2"));

    // Market Buy for 15 shares
    auto buy = createOrder(3, "AAPL", Side::Buy, OrderType::Market, 0, 15, "USER");
    auto trades = engine.match(buy, book);

    EXPECT_EQ(trades.size(), 2);
    EXPECT_EQ(buy->filled_qty, 15);
    EXPECT_EQ(buy->status, OrderStatus::Filled);

    // The remaining 5 shares of BOT2 ask should still be in the book
    auto ask_depth = book.getAskDepth(1);
    ASSERT_EQ(ask_depth.size(), 1);
    EXPECT_EQ(ask_depth[0].first, doubleToPrice(151.0));
    EXPECT_EQ(ask_depth[0].second, 5);

    // Market Buy for 100 shares (more than liquidity)
    auto buy_large = createOrder(4, "AAPL", Side::Buy, OrderType::Market, 0, 100, "USER");
    auto trades2 = engine.match(buy_large, book);

    EXPECT_EQ(trades2.size(), 1); // Matches remaining 5 shares
    EXPECT_EQ(buy_large->filled_qty, 5);
    EXPECT_EQ(buy_large->status, OrderStatus::Cancelled); // Unfilled portion cancelled
}

TEST(MatchingEngineTest, CancelOrder) {
    MatchingEngine engine(nullptr);
    OrderBook book("AAPL");

    auto o = createOrder(1, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10);
    book.insert(o);

    EXPECT_TRUE(engine.cancel(1, book));
    EXPECT_EQ(o->status, OrderStatus::Cancelled);
    EXPECT_TRUE(book.empty());
}

TEST(MatchingEngineTest, ModifyOrderPriority) {
    MatchingEngine engine(nullptr);
    OrderBook book("AAPL");

    // 1. Order decreases quantity at same price -> priority kept
    auto o1 = createOrder(1, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10, "USER_1");
    auto o2 = createOrder(2, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10, "USER_2");
    book.insert(o1);
    book.insert(o2);

    std::vector<Trade> trades;
    // Modify o1 quantity to 5
    EXPECT_TRUE(engine.modify(1, doubleToPrice(150.0), 5, book, 100, trades));
    EXPECT_EQ(o1->qty, 5);
    EXPECT_EQ(o1->status, OrderStatus::Modified);

    // Sell 7 shares, FIFO check: o1 should fill 5, o2 fill 2
    auto sell1 = createOrder(3, "AAPL", Side::Sell, OrderType::Limit, doubleToPrice(150.0), 7, "BOT");
    auto trades_match = engine.match(sell1, book);
    EXPECT_EQ(trades_match.size(), 2);
    EXPECT_EQ(o1->status, OrderStatus::Filled);
    EXPECT_EQ(o2->filled_qty, 2);

    // Clean up
    book.erase(2);

    // 2. Order price changes -> priority lost
    auto o3 = createOrder(4, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10, "USER_3");
    auto o4 = createOrder(5, "AAPL", Side::Buy, OrderType::Limit, doubleToPrice(150.0), 10, "USER_4");
    book.insert(o3);
    book.insert(o4);

    // Modify o3 price to 149.0 (priority lost, moved to new level)
    EXPECT_TRUE(engine.modify(4, doubleToPrice(149.0), 10, book, 101, trades));
    
    // Now o4 should be at the front of 150.0 level
    auto best_bid = book.getBestBid();
    EXPECT_EQ(best_bid->id, 5); // o4 has priority now
}

// --- Portfolio Tests ---
TEST(PortfolioTest, LedgerUpdates) {
    Portfolio portfolio(10000.0);
    EXPECT_DOUBLE_EQ(portfolio.getCash(), 10000.0);
    EXPECT_EQ(portfolio.getPositionQty("AAPL"), 0);

    // Test BUY execution
    Trade t1{1, 10, 20, "AAPL", doubleToPrice(150.0), 10, std::chrono::system_clock::now(), "USER", "BOT", Side::Buy, OrderType::Limit};
    portfolio.handleTrade(t1, "USER"); // We bought

    EXPECT_DOUBLE_EQ(portfolio.getCash(), 10000.0 - (10 * 150.0));
    EXPECT_EQ(portfolio.getPositionQty("AAPL"), 10);
    EXPECT_DOUBLE_EQ(portfolio.getCostBasis("AAPL"), 150.0);

    // Test another BUY to verify weighted average cost basis
    Trade t2{2, 11, 21, "AAPL", doubleToPrice(160.0), 10, std::chrono::system_clock::now(), "USER", "BOT", Side::Buy, OrderType::Limit};
    portfolio.handleTrade(t2, "USER"); // We bought 10 more at 160

    EXPECT_EQ(portfolio.getPositionQty("AAPL"), 20);
    EXPECT_DOUBLE_EQ(portfolio.getCostBasis("AAPL"), 155.0); // (10*150 + 10*160)/20

    // Test Market Price update and Unrealized PnL
    portfolio.updateMarketPrice("AAPL", doubleToPrice(157.0));
    EXPECT_DOUBLE_EQ(portfolio.getUnrealizedPnL(), 20 * (157.0 - 155.0));

    // Test SELL execution to realize PnL
    Trade t3{3, 30, 12, "AAPL", doubleToPrice(165.0), 15, std::chrono::system_clock::now(), "BOT", "USER", Side::Buy, OrderType::Limit};
    portfolio.handleTrade(t3, "USER"); // We sold 15 shares at 165

    EXPECT_EQ(portfolio.getPositionQty("AAPL"), 5);
    EXPECT_DOUBLE_EQ(portfolio.getRealizedPnL(), 15 * (165.0 - 155.0));
    EXPECT_DOUBLE_EQ(portfolio.getCostBasis("AAPL"), 155.0); // Cost basis for remaining 5 shares stays same
}

TEST(PortfolioTest, ShortSellingLedger) {
    Portfolio portfolio(10000.0);

    // Test SELL to open short position
    Trade t1{1, 10, 20, "AAPL", doubleToPrice(150.0), 10, std::chrono::system_clock::now(), "BOT", "USER", Side::Sell, OrderType::Limit};
    portfolio.handleTrade(t1, "USER"); // We sold short 10 shares

    EXPECT_EQ(portfolio.getPositionQty("AAPL"), -10);
    EXPECT_DOUBLE_EQ(portfolio.getCostBasis("AAPL"), 150.0);
    EXPECT_DOUBLE_EQ(portfolio.getCash(), 10000.0 + (10 * 150.0));

    // Market drops to 140 -> Unrealized PnL is positive for shorts
    portfolio.updateMarketPrice("AAPL", doubleToPrice(140.0));
    EXPECT_DOUBLE_EQ(portfolio.getUnrealizedPnL(), -10 * (140.0 - 150.0)); // +100

    // BUY to cover
    Trade t2{2, 30, 40, "AAPL", doubleToPrice(135.0), 10, std::chrono::system_clock::now(), "USER", "BOT", Side::Sell, OrderType::Limit};
    portfolio.handleTrade(t2, "USER"); // Covered completely

    EXPECT_EQ(portfolio.getPositionQty("AAPL"), 0);
    EXPECT_DOUBLE_EQ(portfolio.getRealizedPnL(), 10 * (150.0 - 135.0)); // +150
}

// --- Statistics Tests ---
TEST(StatisticsTest, MathAccuracy) {
    Statistics stats;
    stats.recordOrderReceived();
    stats.recordOrderReceived();

    Trade t1{1, 10, 20, "AAPL", doubleToPrice(100.0), 10, std::chrono::system_clock::now(), "U1", "U2", Side::Buy, OrderType::Limit};
    Trade t2{2, 11, 21, "AAPL", doubleToPrice(110.0), 20, std::chrono::system_clock::now(), "U1", "U3", Side::Buy, OrderType::Limit};

    stats.recordTrade(t1);
    stats.recordTrade(t2);

    stats.recordSpread("AAPL", doubleToPrice(1.5));
    stats.recordLatency(120);
    stats.recordLatency(80);

    auto snap = stats.getSnapshot();
    EXPECT_EQ(snap.orders_received, 2);
    EXPECT_EQ(snap.trades_count, 2);
    EXPECT_EQ(snap.total_volume, 30);
    EXPECT_DOUBLE_EQ(snap.avg_latency_us, 100.0);
    EXPECT_EQ(snap.peak_latency_us, 120);

    EXPECT_DOUBLE_EQ(stats.getVWAP("AAPL"), (100.0*10 + 110.0*20)/30);
    EXPECT_EQ(stats.getSpread("AAPL"), doubleToPrice(1.5));
}
