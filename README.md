# ExchangeCore Terminal

A portfolio-quality **electronic exchange simulator** and full-screen **Terminal User Interface (TUI)** built entirely in Modern C++20.

Built to demonstrate professional-grade systems programming: matching engine design, lock-free statistics, event-driven architecture, and real-time terminal rendering — all as a standalone application with zero dependencies beyond a C++ compiler and CMake.

---

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                          ExchangeCore Terminal                               │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────────────────────┐  │
│  │                          TUI Layer (FTXUI)                            │  │
│  │   TerminalApp  ──►  Panels: OrderBook, Stats, Portfolio, Benchmark    │  │
│  │   Theme  ──►  Dark Professional Color Palette                         │  │
│  └─────────────────────────────┬─────────────────────────────────────────┘  │
│                                 │  subscribes via ExchangeEventListener      │
│  ┌──────────────────────────────▼─────────────────────────────────────────┐  │
│  │                          API Layer                                      │  │
│  │   Events: OrderAccepted, TradeExecuted, MarketUpdated, StatsUpdated    │  │
│  │   ExchangeAPI: submitOrder, cancelOrder, modifyOrder, snapshots        │  │
│  └─────────────────────────────┬───────────────────────────────────────────┘  │
│                                 │                                              │
│  ┌──────────────────────────────▼─────────────────────────────────────────┐  │
│  │                          Core Engine (Standalone Library)               │  │
│  │   ExchangeEngine  ─────────────────────────────────────┐               │  │
│  │        │                                               │               │  │
│  │   MatchingEngine         OrderBook (per symbol)    Statistics           │  │
│  │   Price-Time Priority    Bid/Ask Sorted Maps       Lock-free atomics   │  │
│  │   Limit/Market Orders    O(1) Cancel Lookup        VWAP, Spread,       │  │
│  │   Partial/Full Fills     Depth Snapshots           Throughput, Lat     │  │
│  │        │                                               │               │  │
│  │   Portfolio (USER)    MarketSimulator (Background)  BenchmarkRunner    │  │
│  │   PnL Tracking        Bot Order Flow               100K/1M/10M tests  │  │
│  └───────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Dependency Direction (Strict)
```
TUI (FTXUI)  ──►  API (Events + Listeners)  ──►  Core Engine  ──►  Algorithms
                                                        ▲
                                             Zero FTXUI dependencies
```

---

## Features

### Exchange Engine
- **Price-Time Priority Matching** (FIFO within each price level)
- **Limit Orders** — Rest in book, match aggressively at better prices
- **Market Orders** — Sweep all available liquidity, cancel remaining
- **Cancel Orders** — O(1) removal via order ID hash map
- **Modify Orders** — Preserves queue priority on size-decrease, re-queues on price change
- **Partial Fills** — Full tracking of filled vs remaining quantity
- **Multi-symbol** — Each symbol has its own isolated order book

### Terminal UI
- **Full-screen TUI** — FTXUI-powered, no flickering, no terminal clearing hacks
- **Live Order Book** — Sell (red) / Mid Price / Buy (green) with inline bar charts showing depth
- **Live Recent Trades** — Scrolling trade tape with buyer/seller identification
- **Live Statistics Panel** — Orders/sec, VWAP, Spread, Avg/Peak Latency, Memory usage
- **Portfolio Ledger** — Cash, Positions, Cost Basis, Unrealized & Realized PnL
- **Background Market Simulator** — 5 bot clients generating live order flow for AAPL, MSFT, GOOG, BTCUSD, ETHUSD
- **Benchmark Screen** — Async 100K / 1M / 10M order runs with live progress bar
- **Order Entry Modals** — F1 Buy / F2 Sell with Limit and Market order types
- **Cancel Orders Modal** — F3 to view and cancel any active user order
- **Professional Dark Theme** — Custom RGB color palette, no rainbow colors
- <img width="1920" height="1080" alt="image" src="https://github.com/user-attachments/assets/afb2d9b1-9217-4594-8c9e-3dee32da8cdf" />


### Performance
- Lock-free atomic counters for statistics (zero mutex contention in hot path)
- O(1) cancel via `std::unordered_map<OrderID, OrderBookPosition>` lookup table
- Thread-safe event queue — Engine and TUI run on separate threads without blocking
- UI always receives immutable snapshots, never touches internal engine containers
- `std::deque<Order>` at each price level for true FIFO ordering

---

## Benchmark Results (Release Mode, MSVC 2022)

| Orders     | Time (ms)  | Throughput (ord/s) | Avg Latency (µs) | Peak Latency (µs) | Trades      | Memory (MB) |
|------------|------------|-------------------|------------------|--------------------|-------------|-------------|
| 100,000    | 108.56     | 921,153           | 0.37             | 144                | 92,209      | 2.49        |
| 1,000,000  | 1,098.09   | 910,671           | 0.38             | 736                | 956,161     | 8.79        |
| 10,000,000 | 11,694.18  | 855,126           | 0.45             | 3,145              | 9,760,356   | 20.54       |

> Benchmarks run with 80% Limit / 20% Market orders, 15% crossing rate, random walk pricing. Single-threaded engine; no parallelism.

---

## Build Instructions

### Requirements
- **CMake 3.20+**
- **MSVC 2022** (Visual Studio Build Tools) or **GCC 12+ / Clang 14+**
- **Internet access** on first build (CMake fetches FTXUI v5.0.0 and GoogleTest v1.14.0)

### Build

```powershell
# 1. Configure (downloads dependencies automatically)
cmake -B build

# 2. Build all targets (app, tests, benchmarks)
cmake --build build --config Release

# 3. Run the terminal app
.\build\Release\ExchangeCoreApp.exe

# 4. Run unit tests
.\build\Release\ExchangeCoreTests.exe

# 5. Run standalone benchmarks
.\build\Release\ExchangeCoreBenchmarks.exe
```

> Everything fetched automatically — no `vcpkg`, no `conan`, no manual installs.

---

## Keyboard Controls

| Key   | Action                                |
|-------|---------------------------------------|
| `TAB` | Switch focus between sidebar and view |
| `↑↓`  | Navigate markets / cancel order list  |
| `F1`  | Open Buy order form                   |
| `F2`  | Open Sell order form                  |
| `F3`  | Open active order cancellation list   |
| `F4`  | Switch to Benchmark screen            |
| `F5`  | Switch to Portfolio screen            |
| `ESC` | Close modal / dialog                  |
| `Q`   | Quit                                  |

---

## Project Structure

```
ExchangeCore/
├── CMakeLists.txt              # Fetches FTXUI + GoogleTest, builds all targets
│
├── include/
│   ├── API/
│   │   └── Events.hpp          # Event types + ExchangeEventListener interface
│   ├── Benchmarks/
│   │   └── BenchmarkRunner.hpp # Isolated engine benchmark API
│   ├── Core/
│   │   ├── Order.hpp           # Order, Trade types + fixed-point price helpers
│   │   ├── Trade.hpp           # Trade execution record
│   │   ├── OrderBook.hpp       # Double-sided limit order book
│   │   ├── MatchingEngine.hpp  # Price-time priority matching
│   │   ├── Portfolio.hpp       # Multi-asset PnL ledger
│   │   ├── Statistics.hpp      # Lock-free performance statistics
│   │   ├── MarketSimulator.hpp # Background bot order generator
│   │   ├── ThreadSafeQueue.hpp # MPSC queue for engine→TUI events
│   │   └── ExchangeEngine.hpp  # Top-level coordinator API
│   └── TUI/
│       ├── Theme.hpp           # Professional RGB dark color palette
│       └── TerminalApp.hpp     # Full-screen FTXUI application
│
├── src/
│   ├── Core/                   # Engine implementations
│   ├── Benchmarks/             # BenchmarkRunner implementation
│   └── TUI/                    # TerminalApp rendering + interaction
│
├── app/
│   └── main.cpp                # Application entry point
│
├── tests/
│   └── CoreTests.cpp           # 11 GoogleTest unit tests
│
└── benchmarks/
    └── main.cpp                # CLI benchmark runner (100K / 1M / 10M)
```

---

## Test Coverage

| Test Suite         | Tests | Coverage                                                     |
|--------------------|-------|--------------------------------------------------------------|
| `OrderBookTest`    | 2     | Insert/erase, O(1) lookup, depth aggregation, spread/mid    |
| `MatchingEngineTest` | 6   | Full fills, FIFO priority, partial fills, market orders, cancel, modify priority |
| `PortfolioTest`    | 2     | Long/short ledgers, weighted avg cost basis, realized PnL   |
| `StatisticsTest`   | 1     | VWAP math, latency averaging, spread tracking               |
| **Total**          | **11**| **All pass ✓**                                              |

---

## Design Principles Applied

| Principle                | How It's Applied                                                               |
|--------------------------|--------------------------------------------------------------------------------|
| **SOLID**                | Each class has one responsibility; no cross-layer dependencies                 |
| **RAII**                 | Threads managed via RAII wrappers; no raw `new`/`delete`                       |
| **Dependency Inversion** | TUI depends on abstract `ExchangeEventListener` interface, not the engine impl |
| **Value Semantics**      | `StatisticsSnapshot` and `Trade` are plain structs passed by value             |
| **const correctness**    | All snapshot queries are `const`; no mutable references leaked to TUI          |
| **Zero-cost abstractions**| `std::visit` on `std::variant<Event>` — fully inlined by optimizer            |
| **Move Semantics**       | Orders moved through the pipeline; strings moved into events                   |
| **Lock-free statistics** | `std::atomic<uint64_t>` counters for hot path; mutex only for VWAP aggregates  |

---

## Future Roadmap

- [ ] **REST API Server** — Expose engine via HTTP using cpp-httplib (no Core changes needed)
- [ ] **WebSocket Feed** — Real-time order book and trade streaming
- [ ] **Python Bindings** — Expose Core library via pybind11 for AI/RL agents
- [ ] **SQLite Persistence** — Persist trade history and portfolio across sessions
- [ ] **FIX Protocol** — Standard industry messaging format support
- [ ] **Multi-threaded Matching** — Per-symbol matching threads with lock-free queues
- [ ] **GUI Client** — Native ImGui-based desktop frontend using the same API
- [ ] **Order Types** — Stop, Stop-Limit, IOC, FOK, GTC, AON orders
- [ ] **Level 2 Market Depth** — Full aggregated book publishing
- [ ] **Historical Replay** — Load and replay tick data files

---

## License

MIT License — Free for personal, academic, and commercial use.
