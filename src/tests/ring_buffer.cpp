#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <string>
#include <iomanip>
#include <algorithm>  // For std::sort
#include "../core/ring_buffer.h"

// More realistic market data structures
struct MarketTick {
    int64_t timestamp_ns;
    std::string symbol;
    double price;
    double quantity;
    char side;  // 'B' for buy, 'S' for sell
    
    // For testing equality in our assertions
    bool operator==(const MarketTick& other) const {
        return timestamp_ns == other.timestamp_ns &&
               symbol == other.symbol &&
               price == other.price &&
               quantity == other.quantity &&
               side == other.side;
    }
};

void basic_functionality_test() {
    LockFreeRingBuffer<MarketTick, 4> buffer;  // Size 4 means 3 usable slots
    
    // Test empty buffer
    MarketTick result{};
    assert(!buffer.TryPop(&result));
    
    // Create some test data
    MarketTick tick1{1234567890, "BTCUSD", 50000.0, 1.5, 'B'};
    MarketTick tick2{1234567891, "ETHUSD", 3000.0, 10.0, 'S'};
    MarketTick tick3{1234567892, "BTCUSD", 50010.0, 0.5, 'B'};
    
    // Test push and pop
    assert(buffer.TryPush(tick1));
    assert(buffer.TryPush(tick2));
    assert(buffer.TryPush(tick3));
    assert(!buffer.TryPush({}));  // Should be full
    
    assert(buffer.TryPop(&result));
    assert(result == tick1);
    assert(buffer.TryPop(&result));
    assert(result == tick2);
    
    // Push after pop
    MarketTick tick4{1234567893, "ETHUSD", 3010.0, 5.0, 'B'};
    MarketTick tick5{1234567894, "BTCUSD", 49990.0, 2.0, 'S'};
    
    assert(buffer.TryPush(tick4));
    assert(buffer.TryPush(tick5));
    assert(!buffer.TryPush({}));  // Should be full again
    
    // Empty the buffer
    assert(buffer.TryPop(&result));
    assert(result == tick3);
    assert(buffer.TryPop(&result));
    assert(result == tick4);
    assert(buffer.TryPop(&result));
    assert(result == tick5);
    assert(!buffer.TryPop(&result));  // Should be empty
}

void market_data_pipeline_test() {
    constexpr size_t NUM_TICKS = 5000;
    constexpr size_t BUFFER_SIZE = 1024;
    
    LockFreeRingBuffer<MarketTick, BUFFER_SIZE> buffer;  // Only use one buffer
    
    std::atomic<bool> start{false};
    std::atomic<int> producer_count{0};
    std::atomic<int> consumer_count{0};
    std::vector<int64_t> latencies;
    latencies.reserve(NUM_TICKS);
    
    std::cout << "  Starting producer/consumer test with " << NUM_TICKS << " ticks..." << std::endl;
    
    // Producer thread - simulates market data feed
    std::thread producer([&]() {
        std::cout << "  Producer thread started" << std::endl;
        while (!start) { std::this_thread::yield(); }
        
        for (size_t i = 1; i <= NUM_TICKS; ++i) {
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
            int64_t timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            
            // Alternate between BTC and ETH
            std::string symbol = (i % 2 == 0) ? "BTCUSD" : "ETHUSD";
            
            // Create somewhat realistic price movement
            double base_price = (symbol == "BTCUSD") ? 50000.0 : 3000.0;
            double price_noise = (std::rand() % 100 - 50) / 10.0;
            double quantity = 0.1 + (std::rand() % 100) / 10.0;
            char side = (std::rand() % 2 == 0) ? 'B' : 'S';
            
            MarketTick tick{
                timestamp,
                symbol,
                base_price + price_noise,
                quantity,
                side
            };
            
            while (!buffer.TryPush(tick)) { std::this_thread::yield(); }
            producer_count++;
            
            // Add a small sleep every 100 ticks to avoid overwhelming the consumer
            if (i % 100 == 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(10));
            }
            
            if (i % 1000 == 0) {
                std::cout << "  Producer: " << i << " ticks sent" << std::endl;
            }
        }
        std::cout << "  Producer finished" << std::endl;
    });
    
    // Consumer thread - processes market data
    std::thread consumer([&]() {
        std::cout << "  Consumer thread started" << std::endl;
        while (!start) { std::this_thread::yield(); }
        
        MarketTick tick;
        for (size_t i = 1; i <= NUM_TICKS; ++i) {
            while (!buffer.TryPop(&tick)) { std::this_thread::yield(); }
            
            // Record processing latency
            auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
            int64_t process_ts = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
            int64_t latency = process_ts - tick.timestamp_ns;
            latencies.push_back(latency);
            
            // Simulate some processing
            double processed_price = tick.price * tick.quantity;  // Just do some basic calculation
            
            consumer_count++;
            
            if (i % 1000 == 0) {
                std::cout << "  Consumer: " << i << " ticks processed" << std::endl;
            }
        }
        std::cout << "  Consumer finished" << std::endl;
    });
    
    std::cout << "  Synchronizing threads and starting test..." << std::endl;
    // Start test
    start = true;
    
    std::cout << "  Waiting for threads to complete..." << std::endl;
    producer.join();
    consumer.join();
    
    // Calculate latency stats
    std::cout << "  Calculating statistics..." << std::endl;
    std::sort(latencies.begin(), latencies.end());
    int64_t min_latency = latencies.front();
    int64_t max_latency = latencies.back();
    int64_t median_latency = latencies[latencies.size() / 2];
    int64_t p99_latency = latencies[latencies.size() * 99 / 100];
    
    std::cout << "Market data pipeline test results:\n";
    std::cout << "Producer pushed: " << producer_count << " ticks\n";
    std::cout << "Consumer processed: " << consumer_count << " ticks\n";
    std::cout << "Latency statistics (ns):\n";
    std::cout << "  Min: " << min_latency << "\n";
    std::cout << "  Median: " << median_latency << "\n";
    std::cout << "  99th percentile: " << p99_latency << "\n";
    std::cout << "  Max: " << max_latency << "\n";
}

int main() {
    std::cout << "Testing basic functionality..." << std::endl;
    basic_functionality_test();
    std::cout << "Basic functionality tests passed!" << std::endl;
    
    std::cout << "\nTesting market data pipeline..." << std::endl;
    market_data_pipeline_test();
    std::cout << "Market data pipeline tests passed!" << std::endl;
    
    return 0;
}
