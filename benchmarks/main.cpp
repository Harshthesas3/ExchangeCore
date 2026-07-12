#include "Benchmarks/BenchmarkRunner.hpp"
#include <iostream>
#include <iomanip>
#include <vector>

int main() {
    std::cout << "=========================================================\n";
    std::cout << " ExchangeCore System Benchmark Runner\n";
    std::cout << "=========================================================\n";
    std::cout << "Running isolated engine benchmarks...\n\n";

    std::vector<uint64_t> runs = {100000, 1000000, 10000000}; // 100K, 1M, 10M orders

    std::cout << std::left << std::setw(12) << "Orders" 
              << std::setw(15) << "Time (ms)" 
              << std::setw(18) << "Throughput (op/s)" 
              << std::setw(15) << "Avg Lat (us)" 
              << std::setw(15) << "Peak Lat (us)" 
              << std::setw(12) << "Trades" 
              << std::setw(15) << "Memory (MB)" << "\n";
    std::cout << std::string(102, '-') << "\n";

    for (uint64_t count : runs) {
        std::cout << std::left << std::setw(12) << count << std::flush;
        
        auto result = Exchange::BenchmarkRunner::run(count);

        std::cout << std::left 
                  << std::setw(15) << std::fixed << std::setprecision(2) << result.execution_time_ms
                  << std::setw(18) << std::fixed << std::setprecision(0) << result.orders_per_sec
                  << std::setw(15) << std::fixed << std::setprecision(2) << result.avg_latency_us
                  << std::setw(15) << std::fixed << std::setprecision(0) << result.peak_latency_us
                  << std::setw(12) << result.total_trades
                  << std::setw(15) << std::fixed << std::setprecision(2) << (static_cast<double>(result.memory_used_bytes) / 1024.0 / 1024.0) << "\n";
    }

    std::cout << "=========================================================\n";
    return 0;
}
