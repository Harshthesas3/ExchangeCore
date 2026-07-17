#include "Core/QuantArenaExporter.hpp"
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace Exchange {

QuantArenaExporter::QuantArenaExporter(std::shared_ptr<SessionManager> session_mgr, const std::string& export_dir)
    : session_mgr_(std::move(session_mgr)), export_dir_(export_dir) {
    
    std::filesystem::create_directories(export_dir_);
    
    std::string jsonl_path = export_dir_ + "/trades.jsonl";
    jsonl_file_.open(jsonl_path, std::ios::binary); // Ensure no CR/LF translation
    
    if (!jsonl_file_.is_open()) {
        std::cerr << "Failed to open export file: " << jsonl_path << std::endl;
    }
}

QuantArenaExporter::~QuantArenaExporter() {
    finishExport();
}

void QuantArenaExporter::onEvent(const Event& event) {
    if (finished_) return;

    if (std::holds_alternative<TradeExecutedEvent>(event)) {
        const auto& ev = std::get<TradeExecutedEvent>(event);
        writeTrade(ev.trade);
    }
}

std::string QuantArenaExporter::formatDouble(double value) {
    char buf[64];
    // Use %.10g as requested for deterministic float formatting
    snprintf(buf, sizeof(buf), "%.10g", value);
    return std::string(buf);
}

void QuantArenaExporter::writeTrade(const Trade& trade) {
    if (!jsonl_file_.is_open() || finished_) return;

    // Schema limits
    if (seq_ >= 100000 || byte_count_ >= 8 * 1024 * 1024) {
        if (!limit_warning_shown_) {
            std::cerr << "WARNING: QuantArena export limits reached (100k lines or 8MB)." << std::endl;
            limit_warning_shown_ = true;
        }
        return;
    }

    uint64_t ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(trade.timestamp.time_since_epoch()).count();
    std::string side_str = (trade.taker_side == Side::Buy) ? "buy" : "sell";
    std::string type_str = (trade.taker_type == OrderType::Limit) ? "limit" : "market";

    // Build JSON line manually to ensure exact format and determinism
    std::stringstream ss;
    ss << "{\"seq\":" << seq_
       << ",\"ts_ns\":\"" << ts_ns << "\""
       << ",\"symbol\":\"" << trade.symbol << "\""
       << ",\"side\":\"" << side_str << "\""
       << ",\"qty\":" << formatDouble(static_cast<double>(trade.qty))
       << ",\"price\":" << formatDouble(priceToDouble(trade.price))
       << ",\"order_type\":\"" << type_str << "\""
       << ",\"order_id\":\"o" << (trade.taker_side == Side::Buy ? trade.buy_order_id : trade.sell_order_id) << "\""
       << "}\n";

    std::string line = ss.str();
    
    jsonl_file_.write(line.c_str(), line.size());
    sha256_.update(line);
    
    byte_count_ += line.size();
    seq_++;
    
    // Auto-flush occasionally to prevent partial data loss
    if (seq_ % 100 == 0) {
        jsonl_file_.flush();
    }
}

void QuantArenaExporter::finishExport() {
    if (finished_) return;
    finished_ = true;

    if (jsonl_file_.is_open()) {
        jsonl_file_.close();
    }

    std::string sha_hex = sha256_.hex_digest();
    
    // Write sha256 file
    std::ofstream sha_file(export_dir_ + "/trades.jsonl.sha256", std::ios::binary);
    if (sha_file.is_open()) {
        sha_file << sha_hex << "\n";
    }

    // Write manifest
    std::ofstream manifest_file(export_dir_ + "/manifest.json", std::ios::binary);
    if (manifest_file.is_open()) {
        const auto& config = session_mgr_->getConfig();
        std::stringstream ms;
        ms << "{\n"
           << "  \"match_id\": \"" << config.match_id << "\",\n"
           << "  \"seed_hex\": \"" << config.seed_hex << "\",\n"
           << "  \"engine_version\": \"" << config.engine_version << "\",\n"
           << "  \"generator_version\": \"" << config.generator_version << "\",\n"
           << "  \"line_count\": " << seq_ << ",\n"
           << "  \"byte_count\": " << byte_count_ << ",\n"
           << "  \"sha256\": \"" << sha_hex << "\",\n"
           << "  \"opens_at\": \"" << config.opens_at << "\",\n"
           << "  \"closes_at\": \"" << config.closes_at << "\",\n"
           << "  \"starting_capital\": " << formatDouble(config.capital) << "\n"
           << "}\n";
        manifest_file << ms.str();
    }
}

} // namespace Exchange
