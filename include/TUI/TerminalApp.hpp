#pragma once

#include "Core/ExchangeEngine.hpp"
#include "Core/MarketSimulator.hpp"
#include "Core/ThreadSafeQueue.hpp"
#include "API/Events.hpp"
#include "TUI/Theme.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace Exchange::TUI {

enum class ModalType {
    None,
    Buy,
    Sell,
    CancelList
};

struct UIOrderBookLevel {
    Price price;
    Quantity qty;
};

class TerminalApp : public ExchangeEventListener, public std::enable_shared_from_this<TerminalApp> {
public:
    TerminalApp(std::shared_ptr<ExchangeEngine> engine, std::shared_ptr<MarketSimulator> simulator);
    ~TerminalApp() override = default;

    void run();

    // ExchangeEventListener implementation
    void onEvent(const Event& event) override;

private:
    void initUI();
    void processEvents();
    
    // Event handlers
    void handleEvent(const OrderAcceptedEvent& ev);
    void handleEvent(const OrderRejectedEvent& ev);
    void handleEvent(const TradeExecutedEvent& ev);
    void handleEvent(const OrderCancelledEvent& ev);
    void handleEvent(const OrderModifiedEvent& ev);
    void handleEvent(const MarketUpdatedEvent& ev);
    void handleEvent(const StatisticsUpdatedEvent& ev);

    // Modal helpers
    void openOrderModal(Side side);
    void openCancelModal();
    void submitUserOrder(Side side);
    void cancelUserOrder(OrderID id, const std::string& symbol);
    void runBenchmarkAsync();

    // Render Helpers
    ftxui::Element renderHeader();
    ftxui::Element renderLeftPanel();
    ftxui::Element renderCenterPanel();
    ftxui::Element renderRightPanel();
    ftxui::Element renderBottomPanel();
    ftxui::Element renderFooter();
    ftxui::Element renderModalOverlay();

    std::shared_ptr<ExchangeEngine> engine_;
    std::shared_ptr<MarketSimulator> simulator_;

    ThreadSafeQueue<Event> event_queue_;
    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();

    // TUI Cache (Only modified on the UI thread inside processEvents())
    std::string active_symbol_{"AAPL"};
    std::unordered_map<std::string, std::vector<UIOrderBookLevel>> bids_depth_;
    std::unordered_map<std::string, std::vector<UIOrderBookLevel>> asks_depth_;
    std::unordered_map<std::string, Price> spreads_;
    std::unordered_map<std::string, Price> mid_prices_;
    
    std::vector<Trade> recent_trades_;
    StatisticsSnapshot stats_{0, 0, 0, 0.0, 0, 0.0, 0.0};
    Portfolio portfolio_;

    // User's active orders (for cancellation list)
    std::vector<std::shared_ptr<Order>> user_orders_;
    OrderID next_user_order_id_{1};
    std::string terminal_log_message_{"Welcome to ExchangeCore Terminal."};
    std::chrono::system_clock::time_point log_timestamp_;

    // Component Focus tracking
    int active_focus_panel_{0}; // 0 = Left Sidebar Menu, 1 = Center Screen
    int selected_left_menu_{0};
    
    std::vector<std::string> left_menu_items_;
    std::vector<std::string> order_types_{"LIMIT", "MARKET"};

    ModalType active_modal_{ModalType::None};
    int selected_cancel_order_idx_{0};

    // Form inputs for Modals
    std::string input_symbol_{"AAPL"};
    std::string input_price_{"150.00"};
    std::string input_qty_{"10"};
    int selected_order_type_{0}; // 0 = Limit, 1 = Market

    // Benchmark Screen states
    bool benchmark_running_{false};
    double benchmark_progress_{0.0};
    std::string benchmark_result_text_;
    int selected_bench_size_{0}; // 0 = 100K, 1 = 1M, 2 = 10M

    // Settings screen state
    int sim_speed_idx_{1}; // 0 = Slow, 1 = Normal, 2 = Fast
};

} // namespace Exchange::TUI
