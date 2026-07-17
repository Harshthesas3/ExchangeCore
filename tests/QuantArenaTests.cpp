// QuantArenaTests.cpp
// Tests for QuantArena compatibility: determinism, seed parsing, exporter format.

#include <gtest/gtest.h>
#include "Core/SessionConfig.hpp"
#include "Core/SessionManager.hpp"
#include "Core/ExchangeEngine.hpp"
#include "Core/QuantArenaExporter.hpp"
#include "Core/SHA256.hpp"

#include <memory>
#include <string>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace Exchange;

// ---------------------------------------------------------------------------
// Helper: build a session config with a given seed
// ---------------------------------------------------------------------------
static SessionConfig makeConfig(const std::string& seed_hex) {
    SessionConfig cfg;
    cfg.match_id  = "test-match-001";
    cfg.seed_hex  = seed_hex;
    cfg.opens_at  = "00:00:00";
    cfg.closes_at = "01:00:00";
    cfg.capital   = 100000.0;
    return cfg;
}

// ---------------------------------------------------------------------------
// Seed Validation
// ---------------------------------------------------------------------------
TEST(SessionConfigTest, ValidSeedLengths) {
    EXPECT_TRUE(SessionConfig::validateSeed(std::string(16, 'a')));
    EXPECT_TRUE(SessionConfig::validateSeed(std::string(32, 'b')));
    EXPECT_TRUE(SessionConfig::validateSeed(std::string(64, 'c')));
}

TEST(SessionConfigTest, InvalidSeedLengths) {
    EXPECT_FALSE(SessionConfig::validateSeed(""));
    EXPECT_FALSE(SessionConfig::validateSeed(std::string(15, 'a')));  // too short
    EXPECT_FALSE(SessionConfig::validateSeed(std::string(33, 'a')));  // not 16/32/64
    EXPECT_FALSE(SessionConfig::validateSeed(std::string(63, 'a')));  // one short of 64
}

TEST(SessionConfigTest, InvalidSeedChars) {
    EXPECT_FALSE(SessionConfig::validateSeed("deadbeefcafe123g")); // 'g' is invalid
    EXPECT_FALSE(SessionConfig::validateSeed("ZZZZZZZZZZZZZZZZ")); // all invalid
}

TEST(SessionConfigTest, ValidSeedCharsAllCases) {
    // All valid hex chars including uppercase, lowercase, and digits
    EXPECT_TRUE(SessionConfig::validateSeed("0123456789abcdef"));
    EXPECT_TRUE(SessionConfig::validateSeed("0123456789ABCDEF"));
    EXPECT_TRUE(SessionConfig::validateSeed("DeAdBeEfCaFe1234"));
}

// ---------------------------------------------------------------------------
// SessionManager: PRNG reproducibility with same seed
// ---------------------------------------------------------------------------
TEST(SessionManagerTest, SameRandomSequenceWithSameSeed) {
    const std::string seed = "deadbeefcafe12340011223344556677";

    SessionManager sm1(makeConfig(seed));
    SessionManager sm2(makeConfig(seed));

    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(sm1.nextRandom(), sm2.nextRandom())
            << "PRNG diverged at step " << i;
    }
}

TEST(SessionManagerTest, DifferentSeedsGiveDifferentSequences) {
    SessionManager sm1(makeConfig("aaaaaaaaaaaaaaaa"));
    SessionManager sm2(makeConfig("bbbbbbbbbbbbbbbb"));

    bool any_different = false;
    for (int i = 0; i < 100; ++i) {
        if (sm1.nextRandom() != sm2.nextRandom()) {
            any_different = true;
            break;
        }
    }
    EXPECT_TRUE(any_different) << "Two different seeds should diverge";
}

// ---------------------------------------------------------------------------
// SessionManager: Simulated clock monotonicity
// ---------------------------------------------------------------------------
TEST(SessionManagerTest, SimulatedClockMonotone) {
    SessionManager sm(makeConfig("deadbeefcafe1234"));

    auto prev = sm.now();
    for (int i = 0; i < 500; ++i) {
        auto cur = sm.now();
        EXPECT_GE(cur, prev) << "Simulated clock went backwards at step " << i;
        prev = cur;
    }
}

TEST(SessionManagerTest, SimulatedClockIsDetachedFromWallClock) {
    SessionManager sm(makeConfig("deadbeefcafe1234"));

    // The first call should return ns value close to our hard-coded start (1e9 ns)
    auto t = sm.now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        t.time_since_epoch()).count();

    // Start is 1_000_000_000 ns + small advance, should be much less than wall-clock
    EXPECT_GT(ns, 1000000000LL);           // > 1 second since epoch
    EXPECT_LT(ns, 2000000000LL);           // < 2 seconds since epoch (not wall-clock)
}

// ---------------------------------------------------------------------------
// SessionManager: Order ID format
// ---------------------------------------------------------------------------
TEST(SessionManagerTest, OrderIDFormat) {
    SessionManager sm(makeConfig("deadbeefcafe1234"));

    EXPECT_EQ(sm.nextOrderID(), "o0");
    EXPECT_EQ(sm.nextOrderID(), "o1");
    EXPECT_EQ(sm.nextOrderID(), "o2");
}

TEST(SessionManagerTest, OrderIDMaxLength) {
    SessionManager sm(makeConfig("deadbeefcafe1234"));

    // Generate many IDs and ensure all are <=64 chars
    for (int i = 0; i < 1000; ++i) {
        auto id = sm.nextOrderID();
        EXPECT_LE(id.size(), 64u) << "Order ID too long at step " << i;
    }
}

// ---------------------------------------------------------------------------
// Determinism end-to-end: two engines with same seed produce same trades
// ---------------------------------------------------------------------------
static std::vector<Trade> runDeterministicEngine(const std::string& seed, int steps) {
    auto cfg = makeConfig(seed);
    auto sm  = std::make_shared<SessionManager>(cfg);
    ExchangeEngine engine(sm);

    engine.addSymbol("AAPL");

    std::vector<Trade> captured;
    class TradeCapture : public ExchangeEventListener {
    public:
        std::vector<Trade>& out;
        explicit TradeCapture(std::vector<Trade>& o) : out(o) {}
        void onEvent(const Event& ev) override {
            if (std::holds_alternative<TradeExecutedEvent>(ev)) {
                out.push_back(std::get<TradeExecutedEvent>(ev).trade);
            }
        }
    };

    auto cap = std::make_shared<TradeCapture>(captured);
    engine.registerListener(cap);

    // Submit paired limit orders that will match
    auto rng = std::make_shared<SessionManager>(cfg);
    for (int i = 0; i < steps; ++i) {
        auto buy = std::make_shared<Order>();
        buy->id        = static_cast<OrderID>(i * 2);
        buy->symbol    = "AAPL";
        buy->side      = Side::Buy;
        buy->type      = OrderType::Limit;
        buy->price     = doubleToPrice(150.0);
        buy->qty       = 10;
        buy->timestamp = sm->now();
        buy->client_id = "TEST";
        engine.submitOrder(buy);

        auto sell = std::make_shared<Order>();
        sell->id        = static_cast<OrderID>(i * 2 + 1);
        sell->symbol    = "AAPL";
        sell->side      = Side::Sell;
        sell->type      = OrderType::Limit;
        sell->price     = doubleToPrice(150.0);
        sell->qty       = 10;
        sell->timestamp = sm->now();
        sell->client_id = "TEST";
        engine.submitOrder(sell);
    }

    return captured;
}

TEST(DeterminismTest, SameSeedProducesIdenticalTrades) {
    const std::string seed = "deadbeefcafe12340011223344556677";
    constexpr int STEPS = 20;

    auto trades1 = runDeterministicEngine(seed, STEPS);
    auto trades2 = runDeterministicEngine(seed, STEPS);

    ASSERT_EQ(trades1.size(), trades2.size()) << "Trade counts differ";

    for (size_t i = 0; i < trades1.size(); ++i) {
        EXPECT_EQ(trades1[i].price,  trades2[i].price)  << "Price mismatch at trade " << i;
        EXPECT_EQ(trades1[i].qty,    trades2[i].qty)    << "Qty mismatch at trade " << i;
        EXPECT_EQ(trades1[i].symbol, trades2[i].symbol) << "Symbol mismatch at trade " << i;
    }
}

// ---------------------------------------------------------------------------
// SHA256 sanity
// ---------------------------------------------------------------------------
TEST(SHA256Test, KnownDigest) {
    SHA256 h;
    h.update("abc");
    // Known SHA-256 of "abc" — verified against Python hashlib/OpenSSL
    EXPECT_EQ(h.hex_digest(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(SHA256Test, EmptyInputDigest) {
    SHA256 h;
    EXPECT_EQ(h.hex_digest(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(SHA256Test, IncrementalMatchesSingle) {
    SHA256 h1;
    h1.update("hello world");

    SHA256 h2;
    h2.update("hello ");
    h2.update("world");

    EXPECT_EQ(h1.hex_digest(), h2.hex_digest());
}

// ---------------------------------------------------------------------------
// QuantArena Exporter: writes valid JSONL
// ---------------------------------------------------------------------------
TEST(QuantArenaExporterTest, WritesTrades) {
    const std::string seed = "aaaaaaaaaaaaaaaa";
    auto cfg = makeConfig(seed);
    auto sm  = std::make_shared<SessionManager>(cfg);

    const std::string export_dir = "./test_export_tmp";
    std::filesystem::remove_all(export_dir);

    auto exporter = std::make_shared<QuantArenaExporter>(sm, export_dir);

    // Synthesize a trade event
    Trade t;
    t.symbol           = "AAPL";
    t.price            = doubleToPrice(150.0);
    t.qty              = 10;
    t.buy_order_id     = 1;
    t.sell_order_id    = 2;
    t.buyer_client_id  = "USER";
    t.seller_client_id = "BOT";
    t.taker_side       = Side::Buy;
    t.taker_type       = OrderType::Limit;
    t.timestamp        = sm->now();

    exporter->onEvent(TradeExecutedEvent{t});
    exporter->finishExport();

    // Check file exists
    EXPECT_TRUE(std::filesystem::exists(export_dir + "/trades.jsonl"));
    EXPECT_TRUE(std::filesystem::exists(export_dir + "/trades.jsonl.sha256"));
    EXPECT_TRUE(std::filesystem::exists(export_dir + "/manifest.json"));

    // Check content contains expected fields
    {
        std::ifstream f(export_dir + "/trades.jsonl");
        std::string line;
        ASSERT_TRUE(std::getline(f, line));
        EXPECT_NE(line.find("\"seq\":0"), std::string::npos);
        EXPECT_NE(line.find("\"symbol\":\"AAPL\""), std::string::npos);
        EXPECT_NE(line.find("\"side\":\"buy\""), std::string::npos);
        EXPECT_NE(line.find("\"order_type\":\"limit\""), std::string::npos);
        EXPECT_NE(line.find("\"order_id\":\"o"), std::string::npos);
    } // ifstream closed here

    // Destroy exporter before cleanup (Windows: can't delete open file handles)
    exporter.reset();
    std::filesystem::remove_all(export_dir);
}

TEST(QuantArenaExporterTest, SHA256Consistent) {
    // Write same trade twice (two separate exporters) and verify SHA matches
    const std::string seed = "bbbbbbbbbbbbbbbb";

    auto writeLog = [&](const std::string& dir) -> std::string {
        auto cfg = makeConfig(seed);
        auto sm  = std::make_shared<SessionManager>(cfg);
        auto exporter = std::make_shared<QuantArenaExporter>(sm, dir);

        Trade t;
        t.symbol           = "MSFT";
        t.price            = doubleToPrice(300.0);
        t.qty              = 5;
        t.buy_order_id     = 1;
        t.sell_order_id    = 2;
        t.buyer_client_id  = "A";
        t.seller_client_id = "B";
        t.taker_side       = Side::Sell;
        t.taker_type       = OrderType::Market;
        t.timestamp        = sm->now();

        exporter->onEvent(TradeExecutedEvent{t});
        exporter->finishExport();

        std::string sha;
        {
            std::ifstream sha_f(dir + "/trades.jsonl.sha256");
            std::getline(sha_f, sha);
        } // sha_f closed here

        // Destroy exporter before cleanup (Windows: can't delete open file handles)
        exporter.reset();
        std::filesystem::remove_all(dir);
        return sha;
    };

    std::string sha1 = writeLog("./test_sha_tmp1");
    std::string sha2 = writeLog("./test_sha_tmp2");
    EXPECT_EQ(sha1, sha2) << "SHA256 diverged for identical inputs";
    EXPECT_FALSE(sha1.empty());
}
