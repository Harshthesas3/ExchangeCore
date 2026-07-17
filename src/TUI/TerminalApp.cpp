#include "TUI/TerminalApp.hpp"
#include "Benchmarks/BenchmarkRunner.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

namespace Exchange::TUI {

using namespace ftxui;

// Helper to format timestamps
std::string formatTimePoint(const std::chrono::system_clock::time_point& tp) {
    auto time = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_struct;
#ifdef _WIN32
    localtime_s(&tm_struct, &time);
#else
    localtime_r(&time, &tm_struct);
#endif
    std::stringstream ss;
    ss << std::put_time(&tm_struct, "%H:%M:%S");
    return ss.str();
}

std::string formatPrice(Price price) {
    if (price == 0) return "MKT";
    double val = priceToDouble(price);
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << val;
    return ss.str();
}

TerminalApp::TerminalApp(std::shared_ptr<ExchangeEngine> engine, std::shared_ptr<MarketSimulator> simulator)
    : engine_(std::move(engine)), simulator_(std::move(simulator)) {
    log_timestamp_ = std::chrono::system_clock::now();
    left_menu_items_ = {
        "AAPL",
        "MSFT",
        "GOOG",
        "BTCUSD",
        "ETHUSD",
        "---",
        "Simulation",
        "Benchmark",
        "Portfolio",
        "Settings",
        "Match Session"
    };

    // Prepopulate maps
    for (const auto& item : left_menu_items_) {
        if (item != "---" && item != "Simulation" && item != "Benchmark" && item != "Portfolio" && item != "Settings" && item != "Match Session") {
            bids_depth_[item] = {};
            asks_depth_[item] = {};
            spreads_[item] = 0;
            mid_prices_[item] = 0;
        }
    }
}

void TerminalApp::onEvent(const Event& event) {
    event_queue_.push(event);
    screen_.PostEvent(ftxui::Event::Custom);
}

void TerminalApp::run() {
    engine_->registerListener(shared_from_this());

    // Initialize local cache from current engine state
    portfolio_ = engine_->getPortfolio();
    stats_ = engine_->getStatistics();
    recent_trades_ = engine_->getRecentTrades();

    initUI();
}

void TerminalApp::initUI() {
    // BUG 2 FIX: Create fully separate component instances for each modal.
    // Sharing the same component in two Container::Vertical nodes corrupts
    // FTXUI's internal focus tree and causes double-event delivery.

    // --- Buy Modal ---
    auto buy_input_symbol  = Input(&input_symbol_, "Symbol");
    auto buy_input_price   = Input(&input_price_,  "Price");
    auto buy_input_qty     = Input(&input_qty_,    "Quantity");
    auto buy_type_toggle   = Toggle(&order_types_, &selected_order_type_);
    auto btn_buy_submit    = Button("SUBMIT BUY",  [&] { submitUserOrder(Side::Buy); });
    auto btn_buy_cancel    = Button("CLOSE",       [&] { active_modal_ = ModalType::None; });

    auto buy_modal_comp = Container::Vertical({
        buy_input_symbol,
        buy_type_toggle,
        buy_input_price,
        buy_input_qty,
        Container::Horizontal({btn_buy_submit, btn_buy_cancel})
    });

    // --- Sell Modal (own separate instances) ---
    auto sell_input_symbol = Input(&input_symbol_, "Symbol");
    auto sell_input_price  = Input(&input_price_,  "Price");
    auto sell_input_qty    = Input(&input_qty_,    "Quantity");
    auto sell_type_toggle  = Toggle(&order_types_, &selected_order_type_);
    auto btn_sell_submit   = Button("SUBMIT SELL", [&] { submitUserOrder(Side::Sell); });
    auto btn_sell_cancel   = Button("CLOSE",       [&] { active_modal_ = ModalType::None; });

    auto sell_modal_comp = Container::Vertical({
        sell_input_symbol,
        sell_type_toggle,
        sell_input_price,
        sell_input_qty,
        Container::Horizontal({btn_sell_submit, btn_sell_cancel})
    });

    // --- Cancel Modal ---
    auto btn_cancel_order = Button("CANCEL SELECTED", [&] {
        if (!user_orders_.empty() && selected_cancel_order_idx_ >= 0 &&
            selected_cancel_order_idx_ < static_cast<int>(user_orders_.size())) {
            auto order = user_orders_[selected_cancel_order_idx_];
            cancelUserOrder(order->id, order->symbol);
            active_modal_ = ModalType::None;
        }
    });
    auto btn_cancel_close = Button("CLOSE", [&] { active_modal_ = ModalType::None; });

    auto cancel_modal_comp = Container::Vertical({
        Container::Horizontal({btn_cancel_order, btn_cancel_close})
    });

    // --- Simulation screen controls (idx 6) ---
    auto btn_sim_toggle = Button("[TOGGLE SIMULATOR]", [&] {
        if (simulator_->isRunning()) {
            simulator_->stop();
            terminal_log_message_ = "Market simulator stopped.";
        } else {
            simulator_->start();
            terminal_log_message_ = "Market simulator started.";
        }
        log_timestamp_ = std::chrono::system_clock::now();
    });
    // FIX: Use class-member vectors so Toggle pointers remain valid after initUI() returns.
    auto sim_speed_toggle = Toggle(&sim_speeds_, &sim_speed_idx_);
    sim_screen_comp_ = Container::Vertical({btn_sim_toggle});

    // --- Benchmark screen controls (idx 7) ---
    auto btn_run_bench    = Button("[RUN BENCHMARK]", [&] { runBenchmarkAsync(); });
    auto bench_size_toggle = Toggle(&bench_sizes_, &selected_bench_size_);
    bench_screen_comp_ = Container::Vertical({bench_size_toggle, btn_run_bench});

    // --- Settings screen controls (idx 9) — sim speed + portfolio reset ---
    auto btn_reset_portfolio = Button("[RESET PORTFOLIO]", [&] {
        engine_->reset();
        portfolio_ = engine_->getPortfolio();
        stats_     = engine_->getStatistics();
        recent_trades_.clear();
        user_orders_.clear();
        terminal_log_message_ = "Portfolio and engine statistics reset successfully.";
        log_timestamp_ = std::chrono::system_clock::now();
    });
    settings_screen_comp_ = Container::Vertical({sim_speed_toggle, btn_reset_portfolio});

    // --- Match Session screen controls (idx 10) ---
    auto input_match_seed = Input(&input_match_seed_, "Paste hex seed (16/32/64 chars)");
    auto btn_start_match = Button("[START QUANTARENA MATCH]", [&] { startMatchSession(); });
    auto btn_end_match   = Button("[END MATCH]",              [&] { endMatchSession();   });
    auto btn_export_match = Button("[EXPORT QUANTARENA LOG]",  [&] { exportQuantArenaLog(); });
    match_screen_comp_ = Container::Vertical({input_match_seed, btn_start_match, btn_end_match, btn_export_match});

    // --- Post Match Summary Modal ---
    auto btn_export_log    = Button("[EXPORT QUANTARENA LOG]", [&] { exportQuantArenaLog(); });
    auto btn_close_summary = Button("[CLOSE]",                 [&] { active_modal_ = ModalType::None; });
    auto summary_modal_comp = Container::Vertical({
        Container::Horizontal({btn_export_log, btn_close_summary})
    });

    // Gate each modal subtree: excluded from focus traversal when modal is closed.
    auto buy_modal_gated    = Maybe(buy_modal_comp,     [&] { return active_modal_ == ModalType::Buy; });
    auto sell_modal_gated   = Maybe(sell_modal_comp,    [&] { return active_modal_ == ModalType::Sell; });
    auto cancel_modal_gated = Maybe(cancel_modal_comp,  [&] { return active_modal_ == ModalType::CancelList; });
    auto summary_modal_gated = Maybe(summary_modal_comp,[&] { return active_modal_ == ModalType::PostMatchSummary; });

    // Gate each screen's interactive controls — only in focus tree when that screen is active
    // and no modal is open. This prevents Tab from cycling invisible controls.
    auto sim_gated      = Maybe(sim_screen_comp_,      [&] { return active_modal_ == ModalType::None && selected_left_menu_ == 6;  });
    auto bench_gated    = Maybe(bench_screen_comp_,    [&] { return active_modal_ == ModalType::None && selected_left_menu_ == 7;  });
    auto settings_gated = Maybe(settings_screen_comp_, [&] { return active_modal_ == ModalType::None && selected_left_menu_ == 9;  });
    auto match_gated    = Maybe(match_screen_comp_,    [&] { return active_modal_ == ModalType::None && selected_left_menu_ == 10; });

    auto main_container = Container::Vertical({
        buy_modal_gated,
        sell_modal_gated,
        cancel_modal_gated,
        summary_modal_gated,
        sim_gated,
        bench_gated,
        settings_gated,
        match_gated,
    });

    auto renderer = Renderer(main_container, [&] {
        // Core execution loop: process background engine event queue before rendering
        processEvents();

        // Render dashboard elements
        auto header = renderHeader();
        auto left_panel = renderLeftPanel();
        auto center_panel = renderCenterPanel();
        auto right_panel = renderRightPanel();
        auto bottom_panel = renderBottomPanel();
        auto footer = renderFooter();

        auto dashboard = vbox({
            header,
            hbox({
                left_panel,
                separator(),
                center_panel | flex,
                separator(),
                right_panel
            }) | flex,
            separator(),
            bottom_panel,
            footer
        });

        // Overlay Modal dialogs if active
        if (active_modal_ != ModalType::None) {
            return dbox({
                dashboard,
                clear_under(renderModalOverlay()) | center
            });
        }

        return dashboard;
    });

    // Catch keyboard input events
    renderer = CatchEvent(renderer, [&](ftxui::Event event) {
        // Quit — always handled first
        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Character('Q')) {
            screen_.ExitLoopClosure()();
            return true;
        }

        // --- Modal is open: handle Escape + modal-specific keys ---
        if (active_modal_ != ModalType::None) {
            if (event == ftxui::Event::Escape) {
                active_modal_ = ModalType::None;
                return true;
            }
            // Enter submits Buy/Sell modal
            if ((active_modal_ == ModalType::Buy || active_modal_ == ModalType::Sell) &&
                event == ftxui::Event::Return) {
                submitUserOrder(active_modal_ == ModalType::Buy ? Side::Buy : Side::Sell);
                return true;
            }
            // Navigate Cancel modal list
            if (active_modal_ == ModalType::CancelList) {
                if (event == ftxui::Event::ArrowUp) {
                    if (selected_cancel_order_idx_ > 0) --selected_cancel_order_idx_;
                    return true;
                }
                if (event == ftxui::Event::ArrowDown) {
                    if (selected_cancel_order_idx_ < static_cast<int>(user_orders_.size()) - 1)
                        ++selected_cancel_order_idx_;
                    return true;
                }
                if (event == ftxui::Event::Return) {
                    if (!user_orders_.empty() &&
                        selected_cancel_order_idx_ >= 0 &&
                        selected_cancel_order_idx_ < static_cast<int>(user_orders_.size())) {
                        auto order = user_orders_[selected_cancel_order_idx_];
                        cancelUserOrder(order->id, order->symbol);
                        active_modal_ = ModalType::None;
                    }
                    return true;
                }
            }
            // BUG 4 FIX: Do NOT consume Tab when a modal is open.
            // The Buy/Sell modal needs Tab to cycle between its own input fields
            // (Symbol → Type → Price → Qty → Buttons). Returning false here lets
            // the modal's Container::Vertical handle Tab natively via FTXUI.
            return false;
        }

        // --- No modal open: panel-level Tab / Shift+Tab ---
        // BUG 4 FIX: Tab is only consumed as panel-switcher when no modal is open.
        // BUG 5 FIX: Add Shift+Tab (TabReverse) for backward panel cycling.
        if (event == ftxui::Event::Tab) {
            active_focus_panel_ = (active_focus_panel_ + 1) % 2;
            return true;
        }
        if (event == ftxui::Event::TabReverse) {
            const int panel_count = 2;
            active_focus_panel_ = (active_focus_panel_ - 1 + panel_count) % panel_count;
            return true;
        }

        // --- Function key shortcuts (only when no modal) ---
        if (event == ftxui::Event::F1) { openOrderModal(Side::Buy);  return true; }
        if (event == ftxui::Event::F2) { openOrderModal(Side::Sell); return true; }
        if (event == ftxui::Event::F3) { openCancelModal();          return true; }
        if (event == ftxui::Event::F4) { selected_left_menu_ = 7;   return true; } // Benchmark
        if (event == ftxui::Event::F5) { selected_left_menu_ = 8;   return true; } // Portfolio
        if (event == ftxui::Event::F6) { selected_left_menu_ = 10;  return true; } // Match Session

        // --- Sidebar arrow-key navigation (only when sidebar has focus) ---
        if (active_focus_panel_ == 0) {
            if (event == ftxui::Event::ArrowUp) {
                // BUG 8 FIX: Clamp at top instead of wrapping around.
                // Find the previous non-separator item without wrapping.
                int next = selected_left_menu_;
                do {
                    if (next == 0) break; // already at top — stop
                    --next;
                } while (left_menu_items_[next] == "---");
                // Only update if we found a valid (non-separator) item
                if (left_menu_items_[next] != "---") {
                    selected_left_menu_ = next;
                    if (selected_left_menu_ < 5)
                        active_symbol_ = left_menu_items_[selected_left_menu_];
                }
                return true;
            }
            if (event == ftxui::Event::ArrowDown) {
                // BUG 8 FIX: Clamp at bottom instead of wrapping around.
                int last = static_cast<int>(left_menu_items_.size()) - 1;
                int next = selected_left_menu_;
                do {
                    if (next == last) break; // already at bottom — stop
                    ++next;
                } while (left_menu_items_[next] == "---");
                if (left_menu_items_[next] != "---") {
                    selected_left_menu_ = next;
                    if (selected_left_menu_ < 5)
                        active_symbol_ = left_menu_items_[selected_left_menu_];
                }
                return true;
            }
        }

        return false;
    });

    screen_.Loop(renderer);

    engine_->unregisterListener(shared_from_this());
}

void TerminalApp::processEvents() {
    Event ev;
    while (event_queue_.try_pop(ev)) {
        std::visit([this](auto&& arg) { handleEvent(arg); }, ev);
    }
}

// Event handlers updating UI local caches
void TerminalApp::handleEvent(const OrderAcceptedEvent& ev) {
    if (ev.client_id == "USER") {
        // Add to active user orders
        bool exists = false;
        for (const auto& o : user_orders_) {
            if (o->id == ev.order_id) { exists = true; break; }
        }
        if (!exists) {
            auto order = std::make_shared<Order>();
            order->id = ev.order_id;
            order->symbol = ev.symbol;
            order->side = ev.side;
            order->price = ev.price;
            order->qty = ev.qty;
            order->filled_qty = 0;
            order->client_id = ev.client_id;
            user_orders_.push_back(order);
        }

        std::stringstream ss;
        ss << "Order Accepted: " << (ev.side == Side::Buy ? "BUY " : "SELL ")
           << ev.qty << " " << ev.symbol << " @ " << formatPrice(ev.price);
        terminal_log_message_ = ss.str();
        log_timestamp_ = std::chrono::system_clock::now();
    }
}

void TerminalApp::handleEvent(const OrderRejectedEvent& ev) {
    if (ev.client_id == "USER") {
        terminal_log_message_ = "Order Rejected: " + ev.reason;
        log_timestamp_ = std::chrono::system_clock::now();
    }
}

void TerminalApp::handleEvent(const TradeExecutedEvent& ev) {
    // Save to rolling logs
    recent_trades_.insert(recent_trades_.begin(), ev.trade);
    if (recent_trades_.size() > 50) {
        recent_trades_.pop_back();
    }

    // Update user active orders if involved
    for (auto it = user_orders_.begin(); it != user_orders_.end();) {
        auto& o = *it;
        if (o->id == ev.trade.buy_order_id || o->id == ev.trade.sell_order_id) {
            o->filled_qty += ev.trade.qty;
            if (o->isFilled()) {
                std::stringstream ss;
                ss << "Order Filled: " << o->symbol << " " << (o->side == Side::Buy ? "BUY" : "SELL")
                   << " " << o->qty << " shares.";
                terminal_log_message_ = ss.str();
                log_timestamp_ = std::chrono::system_clock::now();
                it = user_orders_.erase(it);
                continue;
            }
        }
        ++it;
    }

    // Query updated portfolio ledger
    portfolio_ = engine_->getPortfolio();
}

void TerminalApp::handleEvent(const OrderCancelledEvent& ev) {
    if (ev.client_id == "USER") {
        user_orders_.erase(
            std::remove_if(user_orders_.begin(), user_orders_.end(),
                           [&](const auto& o) { return o->id == ev.order_id; }),
            user_orders_.end()
        );

        terminal_log_message_ = "Order Cancelled: ID " + std::to_string(ev.order_id);
        log_timestamp_ = std::chrono::system_clock::now();
    }
}

void TerminalApp::handleEvent(const OrderModifiedEvent& ev) {
    if (ev.client_id == "USER") {
        for (auto& o : user_orders_) {
            if (o->id == ev.order_id) {
                o->price = ev.new_price;
                o->qty = ev.new_qty;
                break;
            }
        }
        terminal_log_message_ = "Order Modified: ID " + std::to_string(ev.order_id);
        log_timestamp_ = std::chrono::system_clock::now();
    }
}

void TerminalApp::handleEvent(const MarketUpdatedEvent& ev) {
    // Cache depth and statistics
    auto& bids = bids_depth_[ev.symbol];
    bids.clear();
    for (const auto& [price, qty] : ev.bids) {
        bids.push_back({price, qty});
    }

    auto& asks = asks_depth_[ev.symbol];
    asks.clear();
    for (const auto& [price, qty] : ev.asks) {
        asks.push_back({price, qty});
    }

    spreads_[ev.symbol] = ev.spread;
    mid_prices_[ev.symbol] = ev.mid_price;

    portfolio_.updateMarketPrice(ev.symbol, ev.mid_price);
}

void TerminalApp::handleEvent(const StatisticsUpdatedEvent& ev) {
    stats_.orders_received = ev.orders_received;
    stats_.trades_count = ev.trades_count;
    stats_.total_volume = ev.total_volume;
    stats_.avg_latency_us = ev.avg_latency_us;
    stats_.peak_latency_us = ev.peak_latency_us;
    stats_.orders_per_sec = ev.orders_per_sec;
    stats_.trades_per_sec = ev.trades_per_sec;
}

// Modal control helpers
void TerminalApp::openOrderModal(Side side) {
    input_symbol_ = active_symbol_;
    input_price_ = mid_prices_[active_symbol_] > 0 ? formatPrice(mid_prices_[active_symbol_]) : "150.00";
    input_qty_ = "10";
    selected_order_type_ = 0; // default Limit
    active_modal_ = (side == Side::Buy) ? ModalType::Buy : ModalType::Sell;
}

void TerminalApp::openCancelModal() {
    selected_cancel_order_idx_ = 0;
    active_modal_ = ModalType::CancelList;
}

void TerminalApp::submitUserOrder(Side side) {
    active_modal_ = ModalType::None;

    std::string symbol = input_symbol_;
    std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);

    if (!engine_->hasSymbol(symbol)) {
        terminal_log_message_ = "Error: Symbol " + symbol + " not traded.";
        log_timestamp_ = std::chrono::system_clock::now();
        return;
    }

    try {
        Quantity qty = std::stoll(input_qty_);
        Price price = 0;
        OrderType type = (selected_order_type_ == 1) ? OrderType::Market : OrderType::Limit;

        if (type == OrderType::Limit) {
            price = doubleToPrice(std::stod(input_price_));
        }

        if (qty <= 0) throw std::invalid_argument("Quantity must be positive");
        if (type == OrderType::Limit && price <= 0) throw std::invalid_argument("Price must be positive");

        auto order = std::make_shared<Order>();
        order->id = next_user_order_id_++;
        order->symbol = std::move(symbol);
        order->side = side;
        order->type = type;
        order->price = price;
        order->qty = qty;
        order->timestamp = std::chrono::system_clock::now();
        order->client_id = "USER";

        // Non-blocking submission
        engine_->submitOrder(order);
    } catch (const std::exception& ex) {
        terminal_log_message_ = std::string("Order submission failed: ") + ex.what();
        log_timestamp_ = std::chrono::system_clock::now();
    }
}

void TerminalApp::cancelUserOrder(OrderID id, const std::string& symbol) {
    engine_->cancelOrder(id, symbol);
}

void TerminalApp::runBenchmarkAsync() {
    if (benchmark_running_) return;

    benchmark_running_ = true;
    benchmark_progress_ = 0.0;
    benchmark_result_text_ = "Benchmark starting...";

    uint64_t count = 100000;
    if (selected_bench_size_ == 1) count = 1000000;
    else if (selected_bench_size_ == 2) count = 10000000;

    std::thread([this, count] {
        auto result = BenchmarkRunner::run(count, [this](double progress) {
            benchmark_progress_ = progress;
            screen_.PostEvent(ftxui::Event::Custom);
        });

        std::stringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Benchmark Completed Successfully!\n"
           << "----------------------------------------\n"
           << "Total Orders:   " << result.total_orders << "\n"
           << "Elapsed Time:   " << result.execution_time_ms << " ms\n"
           << "Throughput:     " << std::fixed << std::setprecision(0) << result.orders_per_sec << " orders/sec\n"
           << "Avg Latency:    " << std::fixed << std::setprecision(2) << result.avg_latency_us << " us\n"
           << "Peak Latency:   " << std::fixed << std::setprecision(0) << result.peak_latency_us << " us\n"
           << "Trades Exec:    " << result.total_trades << "\n"
           << "Memory Alloc:   " << std::fixed << std::setprecision(2) << (static_cast<double>(result.memory_used_bytes) / 1024.0 / 1024.0) << " MB";
        
        benchmark_result_text_ = ss.str();
        benchmark_running_ = false;
        screen_.PostEvent(ftxui::Event::Custom);
    }).detach();
}

// Render panel layout functions
Element TerminalApp::renderHeader() {
    std::string sim_status = simulator_->isRunning() ? "RUNNING" : "STOPPED";
    auto time_str = formatTimePoint(std::chrono::system_clock::now());

    return hbox({
        text(" ExchangeCore Terminal ") | bold | color(Theme::TextLight) | bgcolor(Theme::ActiveHeaderBg),
        separator(),
        text(" Market: " + active_symbol_) | bold | color(Theme::InfoCyan),
        separator(),
        text(" Connection: LOCAL ") | color(Theme::ProfitGreen),
        separator(),
        text(" Simulator: " + sim_status) | color(simulator_->isRunning() ? Theme::ProfitGreen : Theme::WarningYellow),
        filler(),
        text(" Time: " + time_str + " ") | color(Theme::TextMuted)
    }) | borderStyled(Theme::Border);
}

Element TerminalApp::renderLeftPanel() {
    Elements menu_elems;
    for (size_t i = 0; i < left_menu_items_.size(); ++i) {
        std::string item = left_menu_items_[i];
        if (item == "---") {
            menu_elems.push_back(separator());
            continue;
        }

        bool is_selected = (static_cast<int>(i) == selected_left_menu_);
        bool sidebar_focused = (active_focus_panel_ == 0);

        auto element = text(" " + item + " ");
        if (is_selected) {
            if (sidebar_focused) {
                element = element | bold | color(Theme::TextLight) | bgcolor(Theme::SelectionBlue);
            } else {
                element = element | bold | color(Theme::TextLight) | bgcolor(Theme::ActiveHeaderBg);
            }
        } else {
            element = element | color(Theme::TextMuted);
        }
        menu_elems.push_back(element);
    }

    // FIX: Sidebar was 20 chars wide — header "NAVIGATION [TAB to focus]" (25 chars)
    // overflowed, wrapping to 2 lines and clipping the last items (including Match Session).
    // Fix: shorten focus label and widen the panel to 24 chars.
    bool sidebar_active = (active_focus_panel_ == 0);
    auto nav_header = hbox({
        text(sidebar_active ? "\u25b6 NAVIGATION" : "  NAVIGATION") | bold | color(sidebar_active ? Theme::InfoCyan : Theme::TextMuted),
    });

    return vbox({
        nav_header,
        separator(),
        vbox(std::move(menu_elems)),
        filler()
    }) | size(WIDTH, EQUAL, 24) | borderStyled(Theme::Border);
}

Element TerminalApp::renderCenterPanel() {
    // If selected_left_menu_ is a market: render Order Book
    if (selected_left_menu_ < 5) {
        const auto& bids = bids_depth_[active_symbol_];
        const auto& asks = asks_depth_[active_symbol_];

        Elements ask_rows;
        // Asks are displayed highest price at top, lowest price at bottom
        auto sorted_asks = asks;
        std::sort(sorted_asks.begin(), sorted_asks.end(), [](const auto& a, const auto& b) {
            return a.price > b.price; // Descending to show highest ask at the top
        });

        Quantity max_qty = 1;
        for (const auto& ask : asks) max_qty = std::max(max_qty, ask.qty);
        for (const auto& bid : bids) max_qty = std::max(max_qty, bid.qty);

        for (const auto& ask : sorted_asks) {
            double relative_bar_width = static_cast<double>(ask.qty) / max_qty;
            int bar_chars = static_cast<int>(relative_bar_width * 15);
            std::string bar(bar_chars, '#');

            ask_rows.push_back(hbox({
                text(formatPrice(ask.price)) | color(Theme::LossRed) | size(WIDTH, EQUAL, 12),
                separator(),
                text(std::to_string(ask.qty)) | size(WIDTH, EQUAL, 10) | align_right,
                separator(),
                text(bar) | color(Theme::LossRed)
            }));
        }

        if (ask_rows.empty()) {
            ask_rows.push_back(text("No asks available.") | color(Theme::TextMuted));
        }

        Elements bid_rows;
        // Bids are displayed highest price at top, lowest price at bottom
        auto sorted_bids = bids;
        std::sort(sorted_bids.begin(), sorted_bids.end(), [](const auto& a, const auto& b) {
            return a.price > b.price; // Descending
        });

        for (const auto& bid : sorted_bids) {
            double relative_bar_width = static_cast<double>(bid.qty) / max_qty;
            int bar_chars = static_cast<int>(relative_bar_width * 15);
            std::string bar(bar_chars, '#');

            bid_rows.push_back(hbox({
                text(formatPrice(bid.price)) | color(Theme::ProfitGreen) | size(WIDTH, EQUAL, 12),
                separator(),
                text(std::to_string(bid.qty)) | size(WIDTH, EQUAL, 10) | align_right,
                separator(),
                text(bar) | color(Theme::ProfitGreen)
            }));
        }

        if (bid_rows.empty()) {
            bid_rows.push_back(text("No bids available.") | color(Theme::TextMuted));
        }

        Price mid = mid_prices_[active_symbol_];
        Price spread = spreads_[active_symbol_];
        std::string mid_desc = "MID PRICE: " + formatPrice(mid) + " (Spread: " + formatPrice(spread) + ")";

        return vbox({
            text(" SELL BOOK ") | bold | color(Theme::LossRed),
            separator(),
            vbox(std::move(ask_rows)) | flex,
            separator(),
            text(mid_desc) | bold | color(Theme::WarningYellow) | center,
            separator(),
            text(" BUY BOOK ") | bold | color(Theme::ProfitGreen),
            separator(),
            vbox(std::move(bid_rows)) | flex
        }) | borderStyled(Theme::Border);
    }
    
    // Simulation Screen
    if (selected_left_menu_ == 6) {
        std::string sim_state = simulator_->isRunning() ? "\u25cf RUNNING" : "\u25cb STOPPED";
        auto color_mode = simulator_->isRunning() ? Theme::ProfitGreen : Theme::LossRed;

        return vbox({
            text(" MARKET SIMULATOR CONTROL ") | bold | color(Theme::TextLight),
            separator(),
            vbox({
                hbox({text("Status: "), text(sim_state) | bold | color(color_mode)}),
                separator(),
                text("Press [TOGGLE SIMULATOR] to start or stop the bot market maker:") | color(Theme::TextMuted),
                sim_screen_comp_->Render() | borderStyled(Theme::Border),
                separator(),
                text("Simulator speed (adjusted in Settings):") | color(Theme::TextMuted),
                hbox({text("  Current: "), text(sim_speeds_[sim_speed_idx_]) | bold | color(Theme::InfoCyan)}),
                filler()
            }) | flex
        }) | borderStyled(Theme::Border);
    }

    // Benchmark Screen
    if (selected_left_menu_ == 7) {
        Elements result_lines;
        if (!benchmark_result_text_.empty()) {
            std::stringstream ss(benchmark_result_text_);
            std::string line;
            while (std::getline(ss, line)) {
                result_lines.push_back(text(line));
            }
        }

        auto progress_bar = gauge(benchmark_progress_ / 100.0);
        std::stringstream prog_ss;
        prog_ss << "Progress: " << std::fixed << std::setprecision(1) << benchmark_progress_ << "%";

        return vbox({
            text(" ENGINE PERFORMANCE BENCHMARK ") | bold | color(Theme::TextLight),
            separator(),
            vbox({
                text("Select order count, then press [RUN BENCHMARK]:") | color(Theme::TextMuted),
                bench_screen_comp_->Render() | borderStyled(Theme::Border),
                separator(),
                benchmark_running_ ? vbox({
                    text(prog_ss.str()) | bold | color(Theme::InfoCyan),
                    progress_bar | color(Theme::InfoCyan)
                }) : text("Status: Idle") | color(Theme::TextMuted),
                separator(),
                text("Results:") | bold,
                vbox(std::move(result_lines)) | borderStyled(Theme::Border),
                filler()
            }) | flex
        }) | borderStyled(Theme::Border);
    }

    // Portfolio Screen
    if (selected_left_menu_ == 8) {
        Elements position_rows;
        // Table Header
        position_rows.push_back(hbox({
            text("Symbol") | bold | size(WIDTH, EQUAL, 10),
            separator(),
            text("Shares") | bold | size(WIDTH, EQUAL, 10) | align_right,
            separator(),
            text("Cost Basis") | bold | size(WIDTH, EQUAL, 12) | align_right,
            separator(),
            text("Mkt Price") | bold | size(WIDTH, EQUAL, 12) | align_right,
            separator(),
            text("Unreal PnL") | bold | size(WIDTH, EQUAL, 12) | align_right
        }));
        position_rows.push_back(separator());

        auto active_positions = portfolio_.getActivePositions();
        for (const auto& pos : active_positions) {
            auto pnl_color = pos.unrealized_pnl >= 0 ? Theme::ProfitGreen : Theme::LossRed;
            std::stringstream basis_ss, price_ss, pnl_ss;
            basis_ss << std::fixed << std::setprecision(2) << pos.cost_basis;
            price_ss << std::fixed << std::setprecision(2) << pos.market_price;
            pnl_ss << std::fixed << std::setprecision(2) << pos.unrealized_pnl;

            position_rows.push_back(hbox({
                text(pos.symbol) | size(WIDTH, EQUAL, 10),
                separator(),
                text(std::to_string(pos.qty)) | size(WIDTH, EQUAL, 10) | align_right,
                separator(),
                text(basis_ss.str()) | size(WIDTH, EQUAL, 12) | align_right,
                separator(),
                text(price_ss.str()) | size(WIDTH, EQUAL, 12) | align_right,
                separator(),
                text(pnl_ss.str()) | size(WIDTH, EQUAL, 12) | align_right | color(pnl_color)
            }));
        }

        if (active_positions.empty()) {
            position_rows.push_back(text("No active open positions.") | color(Theme::TextMuted));
        }

        double total_pnl = portfolio_.getRealizedPnL() + portfolio_.getUnrealizedPnL();
        auto tot_pnl_color = total_pnl >= 0 ? Theme::ProfitGreen : Theme::LossRed;

        std::stringstream cash_ss, val_ss, realized_ss, unrealized_ss, tot_ss;
        cash_ss << std::fixed << std::setprecision(2) << portfolio_.getCash();
        val_ss << std::fixed << std::setprecision(2) << portfolio_.getTotalValue();
        realized_ss << std::fixed << std::setprecision(2) << portfolio_.getRealizedPnL();
        unrealized_ss << std::fixed << std::setprecision(2) << portfolio_.getUnrealizedPnL();
        tot_ss << std::fixed << std::setprecision(2) << total_pnl;

        return vbox({
            text(" USER PORTFOLIO LEDGER ") | bold | color(Theme::TextLight),
            separator(),
            vbox({
                hbox({
                    vbox({
                        text("ACCOUNT SUMMARY") | bold,
                        separator(),
                        hbox({text("Cash Balance:   $"), text(cash_ss.str()) | bold}),
                        hbox({text("Portfolio Value: $"), text(val_ss.str()) | bold}),
                        hbox({text("Realized PnL:   $"), text(realized_ss.str()) | bold | color(portfolio_.getRealizedPnL() >= 0 ? Theme::ProfitGreen : Theme::LossRed)}),
                        hbox({text("Unrealized PnL: $"), text(unrealized_ss.str()) | bold | color(portfolio_.getUnrealizedPnL() >= 0 ? Theme::ProfitGreen : Theme::LossRed)}),
                        hbox({text("Total Net PnL:  $"), text(tot_ss.str()) | bold | color(tot_pnl_color)})
                    }) | borderStyled(Theme::Border) | size(WIDTH, EQUAL, 40),
                    separator(),
                    vbox({
                        text("ACTIVE POSITIONS") | bold,
                        separator(),
                        vbox(std::move(position_rows))
                    }) | borderStyled(Theme::Border) | flex
                }),
                filler()
            }) | flex
        }) | borderStyled(Theme::Border);
    }

    // Settings Screen
    if (selected_left_menu_ == 9) {
        return vbox({
            text(" ENGINE SETTINGS ") | bold | color(Theme::TextLight),
            separator(),
            vbox({
                text("Simulator speed  (Tab to toggle, Enter to confirm):") | bold,
                text("Adjust how rapidly simulated bots submit random orders.") | color(Theme::TextMuted),
                separator(),
                settings_screen_comp_->Render() | borderStyled(Theme::Border),
                separator(),
                text("[RESET PORTFOLIO] wipes all cash, positions, and trade history.") | color(Theme::WarningYellow),
                filler()
            }) | flex
        }) | borderStyled(Theme::Border);
    }

    // Match Session Screen
    if (selected_left_menu_ == 10) {
        std::string status_text = match_running_
            ? ("\u25cf MATCH RUNNING  |  Seed: " + input_match_seed_)
            : "\u25cb No match session active.";
        auto status_color = match_running_ ? Theme::ProfitGreen : Theme::TextMuted;

        return vbox({
            text(" QUANTARENA MATCH SESSION ") | bold | color(Theme::TextLight),
            separator(),
            vbox({
                text("Paste your QuantArena match seed below (16, 32, or 64 hex chars):") | color(Theme::TextMuted),
                text("Whitespace and 0x prefix are stripped automatically.") | color(Theme::TextMuted),
                separator(),
                match_screen_comp_->Render() | borderStyled(Theme::Border),
                separator(),
                match_error_msg_.empty()
                    ? emptyElement()
                    : text(" \u2717 " + match_error_msg_) | color(Theme::LossRed),
                text(status_text) | bold | color(status_color),
                filler()
            }) | flex
        }) | borderStyled(Theme::Border);
    }

    return vbox({text("Unknown Panel Selected.")}) | borderStyled(Theme::Border);
}

Element TerminalApp::renderRightPanel() {
    double vwap = engine_->getVWAP(active_symbol_);
    Price spread = engine_->getSpread(active_symbol_);

    std::stringstream vwap_ss, spr_ss;
    vwap_ss << std::fixed << std::setprecision(2) << vwap;
    spr_ss << std::fixed << std::setprecision(2) << priceToDouble(spread);

    std::stringstream lat_ss, rate_ss, trd_rate_ss;
    lat_ss << std::fixed << std::setprecision(2) << stats_.avg_latency_us;
    rate_ss << std::fixed << std::setprecision(0) << stats_.orders_per_sec;
    trd_rate_ss << std::fixed << std::setprecision(0) << stats_.trades_per_sec;

    return vbox({
        text(" LIVE STATISTICS ") | bold | color(Theme::TextLight),
        separator(),
        vbox({
            hbox({text("Orders Recv: "), text(std::to_string(stats_.orders_received)) | bold}),
            hbox({text("Trades Exec: "), text(std::to_string(stats_.trades_count)) | bold}),
            hbox({text("Vol Traded:  "), text(std::to_string(stats_.total_volume)) | bold}),
            separator(),
            text("Market Statistics (" + active_symbol_ + "):") | color(Theme::TextMuted),
            hbox({text("Current Spread: "), text(spr_ss.str()) | bold}),
            hbox({text("VWAP Price:     "), text(vwap_ss.str()) | bold}),
            separator(),
            text("Engine Performance:") | color(Theme::TextMuted),
            hbox({text("Avg Latency:  "), text(lat_ss.str() + " us") | bold | color(Theme::InfoCyan)}),
            hbox({text("Peak Latency: "), text(std::to_string(stats_.peak_latency_us) + " us") | bold | color(Theme::WarningYellow)}),
            hbox({text("Throughput:   "), text(rate_ss.str() + " ord/s") | bold | color(Theme::InfoCyan)}),
            hbox({text("Trade Speed:  "), text(trd_rate_ss.str() + " trd/s") | bold}),
            separator(),
            text("CPU/Memory Status:") | color(Theme::TextMuted),
            hbox({text("Memory:       "), text(std::to_string(BenchmarkRunner::getMemoryUsage() / 1024 / 1024) + " MB") | bold})
        }),
        filler()
    }) | size(WIDTH, EQUAL, 32) | borderStyled(Theme::Border);
}

Element TerminalApp::renderBottomPanel() {
    Elements trade_rows;
    // Header
    trade_rows.push_back(hbox({
        text("Time") | bold | size(WIDTH, EQUAL, 10),
        separator(),
        text("Symbol") | bold | size(WIDTH, EQUAL, 8),
        separator(),
        text("Side") | bold | size(WIDTH, EQUAL, 6),
        separator(),
        text("Price") | bold | size(WIDTH, EQUAL, 12) | align_right,
        separator(),
        text("Quantity") | bold | size(WIDTH, EQUAL, 10) | align_right,
        separator(),
        text("Buyer ID") | bold | size(WIDTH, EQUAL, 12),
        separator(),
        text("Seller ID") | bold | size(WIDTH, EQUAL, 12)
    }));
    trade_rows.push_back(separator());

    // Only display matching active trades, or show all trades across symbols
    size_t printed = 0;
    for (const auto& trade : recent_trades_) {
        if (printed >= 6) break; // Limit bottom panel depth
        
        bool is_user_buy = (trade.buyer_client_id == "USER");
        bool is_user_sell = (trade.seller_client_id == "USER");
        auto side_text = (is_user_buy && is_user_sell) ? "CROSS" : (is_user_buy ? "BUY" : (is_user_sell ? "SELL" : "BOT"));
        auto side_color = is_user_buy ? Theme::ProfitGreen : (is_user_sell ? Theme::LossRed : Theme::TextMuted);

        trade_rows.push_back(hbox({
            text(formatTimePoint(trade.timestamp)) | size(WIDTH, EQUAL, 10) | color(Theme::TextMuted),
            separator(),
            text(trade.symbol) | size(WIDTH, EQUAL, 8),
            separator(),
            text(side_text) | size(WIDTH, EQUAL, 6) | color(side_color),
            separator(),
            text(formatPrice(trade.price)) | size(WIDTH, EQUAL, 12) | align_right,
            separator(),
            text(std::to_string(trade.qty)) | size(WIDTH, EQUAL, 10) | align_right,
            separator(),
            text(trade.buyer_client_id) | size(WIDTH, EQUAL, 12) | color(is_user_buy ? Theme::ProfitGreen : Theme::TextMuted),
            separator(),
            text(trade.seller_client_id) | size(WIDTH, EQUAL, 12) | color(is_user_sell ? Theme::LossRed : Theme::TextMuted)
        }));
        printed++;
    }

    if (recent_trades_.empty()) {
        trade_rows.push_back(text("No trades executed yet.") | color(Theme::TextMuted));
    }

    // Logging terminal status message
    auto time_str = formatTimePoint(log_timestamp_);
    auto log_element = hbox({
        text(" [" + time_str + "] System Message: ") | color(Theme::WarningYellow) | bold,
        text(terminal_log_message_) | color(Theme::TextLight)
    });

    return vbox({
        text(" RECENT TRADE LEDGER (ALL MARKETS) ") | bold | color(Theme::TextLight),
        separator(),
        vbox(std::move(trade_rows)) | flex,
        separator(),
        log_element
    }) | size(HEIGHT, EQUAL, 10) | borderStyled(Theme::Border);
}

Element TerminalApp::renderFooter() {
    return hbox({
        text(" F1 ") | bgcolor(Theme::SelectionBlue) | color(Theme::TextLight), text(" Buy  "),
        text(" F2 ") | bgcolor(Theme::SelectionBlue) | color(Theme::TextLight), text(" Sell  "),
        text(" F3 ") | bgcolor(Theme::SelectionBlue) | color(Theme::TextLight), text(" Cancel  "),
        text(" F4 ") | bgcolor(Theme::SelectionBlue) | color(Theme::TextLight), text(" Bench  "),
        text(" F5 ") | bgcolor(Theme::SelectionBlue) | color(Theme::TextLight), text(" Portfolio  "),
        text(" F6 ") | bgcolor(Theme::SelectionBlue) | color(Theme::TextLight), text(" Match  "),
        text(" TAB ") | bgcolor(Theme::ActiveHeaderBg) | color(Theme::TextLight), text(" Panel  "),
        filler(),
        text(" Q ") | bgcolor(Theme::LossRed) | color(Theme::TextLight), text(" Quit ")
    }) | borderStyled(Theme::Border);
}

Element TerminalApp::renderModalOverlay() {
    Elements fields;

    if (active_modal_ == ModalType::Buy || active_modal_ == ModalType::Sell) {
        std::string title = (active_modal_ == ModalType::Buy) ? "SUBMIT LIMIT/MARKET BUY ORDER" : "SUBMIT LIMIT/MARKET SELL ORDER";
        auto title_color = (active_modal_ == ModalType::Buy) ? Theme::ProfitGreen : Theme::LossRed;

        return vbox({
            text(title) | bold | color(title_color) | center,
            separator(),
            hbox({text("  Symbol:    "), text(input_symbol_) | bold}),
            hbox({text("  Order Type:"), text(selected_order_type_ == 0 ? " LIMIT" : " MARKET") | bold}),
            selected_order_type_ == 0 ? hbox({text("  Price:     "), text(input_price_) | bold}) : emptyElement(),
            hbox({text("  Quantity:  "), text(input_qty_) | bold}),
            separator(),
            text("Press keys to adjust inputs in the fields, TAB to cycle, Enter to submit.") | color(Theme::TextMuted) | center,
            separator(),
            text("Press [SUBMIT] or [CANCEL] button or Escape to exit.") | color(Theme::WarningYellow) | center
        }) | size(WIDTH, GREATER_THAN, 50) | size(HEIGHT, GREATER_THAN, 12) | borderStyled(Theme::Border) | bgcolor(Theme::Surface);
    }

    if (active_modal_ == ModalType::CancelList) {
        Elements order_lines;
        for (size_t i = 0; i < user_orders_.size(); ++i) {
            const auto& o = user_orders_[i];
            bool is_sel = (static_cast<int>(i) == selected_cancel_order_idx_);
            std::stringstream ss;
            ss << "[" << o->id << "] " << (o->side == Side::Buy ? "BUY " : "SELL ")
               << o->qty << " " << o->symbol << " @ " << formatPrice(o->price)
               << " (Filled: " << o->filled_qty << ")";

            auto line_elem = text(ss.str());
            if (is_sel) {
                line_elem = line_elem | bold | bgcolor(Theme::SelectionBlue) | color(Theme::TextLight);
            } else {
                line_elem = line_elem | color(Theme::TextLight);
            }
            order_lines.push_back(line_elem);
        }

        if (user_orders_.empty()) {
            order_lines.push_back(text("No active orders found.") | color(Theme::TextMuted) | center);
        }

        return vbox({
            text(" ACTIVE LIMIT ORDERS LIST ") | bold | color(Theme::WarningYellow) | center,
            separator(),
            vbox(std::move(order_lines)) | size(HEIGHT, EQUAL, 8) | vscroll_indicator | frame | borderStyled(Theme::Border),
            separator(),
            text("Arrow Keys to navigate, Enter to Cancel Selected, Escape to close.") | color(Theme::TextMuted) | center
        }) | size(WIDTH, EQUAL, 60) | borderStyled(Theme::Border) | bgcolor(Theme::Surface);
    }

    if (active_modal_ == ModalType::PostMatchSummary) {
        return vbox({
            text(" POST-MATCH SUMMARY ") | bold | color(Theme::WarningYellow) | center,
            separator(),
            text("The QuantArena match simulation has ended.") | center,
            text("You can now export the deterministic trade log to disk.") | center,
            separator(),
            match_exported_ ? text("Export Successful!") | color(Theme::ProfitGreen) | center
                            : text("Ready to export.") | color(Theme::TextMuted) | center,
            separator(),
            text("Press [EXPORT QUANTARENA LOG] button to write trades.jsonl") | color(Theme::TextMuted) | center,
            text("Press [CLOSE] to return to terminal.") | color(Theme::TextMuted) | center
        }) | size(WIDTH, EQUAL, 60) | borderStyled(Theme::Border) | bgcolor(Theme::Surface);
    }

    return emptyElement();
}

void TerminalApp::startMatchSession() {
    // Strip whitespace and optional 0x
    std::string seed = input_match_seed_;
    seed.erase(std::remove_if(seed.begin(), seed.end(), ::isspace), seed.end());
    if (seed.starts_with("0x") || seed.starts_with("0X")) {
        seed = seed.substr(2);
    }

    // Validate
    if (seed.length() != 16 && seed.length() != 32 && seed.length() != 64) {
        match_error_msg_ = "Invalid seed: Must be 16, 32, or 64 hex chars.";
        return;
    }
    for (char c : seed) {
        if (!std::isxdigit(c)) {
            match_error_msg_ = "Invalid seed: Contains non-hex characters.";
            return;
        }
    }

    match_error_msg_ = "";

    // Stop existing simulator
    if (simulator_->isRunning()) {
        simulator_->stop();
    }

    // Unregister old listener
    engine_->unregisterListener(shared_from_this());

    // Create session config
    SessionConfig config;
    config.match_id = "UI-MATCH-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    config.seed_hex = seed;
    config.opens_at = formatTimePoint(std::chrono::system_clock::now());
    for (const auto& sym : config.symbols) {
        config.symbol_universe.insert(sym);
    }

    // Re-instantiate
    session_mgr_ = std::make_shared<SessionManager>(config);
    engine_ = std::make_shared<ExchangeEngine>(session_mgr_);
    simulator_ = std::make_shared<MarketSimulator>(*engine_, session_mgr_);

    // Register new listener
    engine_->registerListener(shared_from_this());

    // Reset UI state
    portfolio_ = engine_->getPortfolio();
    stats_ = engine_->getStatistics();
    recent_trades_.clear();
    user_orders_.clear();
    
    // Create exporter
    exporter_ = std::make_shared<QuantArenaExporter>(session_mgr_, "./quantarena_exports/" + config.match_id);
    engine_->registerListener(exporter_); // Exporter listens for trades

    match_running_ = true;
    match_exported_ = false;
    terminal_log_message_ = "QuantArena Match Started. Seed: " + seed;
    log_timestamp_ = std::chrono::system_clock::now();

    // Start simulator
    simulator_->start();
}

void TerminalApp::endMatchSession() {
    if (!match_running_) return;

    if (simulator_->isRunning()) {
        simulator_->stop();
    }

    match_running_ = false;
    match_end_time_ = session_mgr_->now();
    terminal_log_message_ = "QuantArena Match Ended.";
    log_timestamp_ = std::chrono::system_clock::now();

    active_modal_ = ModalType::PostMatchSummary;
}

void TerminalApp::exportQuantArenaLog() {
    if (exporter_) {
        // Finalize config
        auto& config = session_mgr_->getConfig();
        config.closes_at = formatTimePoint(match_end_time_);
        config.capital = portfolio_.getCash();
        
        exporter_->finishExport();
        match_exported_ = true;
        terminal_log_message_ = "QuantArena Log Exported to ./quantarena_exports/" + config.match_id;
        log_timestamp_ = std::chrono::system_clock::now();
    }
}

} // namespace Exchange::TUI