#pragma once

#include "API/Events.hpp"
#include "Core/SessionManager.hpp"
#include "Core/SHA256.hpp"
#include <fstream>
#include <string>
#include <memory>

namespace Exchange {

class QuantArenaExporter : public ExchangeEventListener {
public:
    QuantArenaExporter(std::shared_ptr<SessionManager> session_mgr, const std::string& export_dir);
    ~QuantArenaExporter() override;

    void onEvent(const Event& event) override;

    void finishExport();

private:
    void writeTrade(const Trade& trade);
    std::string formatDouble(double value);

    std::shared_ptr<SessionManager> session_mgr_;
    std::string export_dir_;
    
    std::ofstream jsonl_file_;
    SHA256 sha256_;
    
    uint64_t seq_{0};
    uint64_t byte_count_{0};
    
    bool limit_warning_shown_{false};
    bool finished_{false};
};

} // namespace Exchange
