// src/main.cpp
int main() {
  // Initialize buffers
  LockFreeRingBuffer<MarketUpdate, 4096> raw_buffer;
  LockFreeRingBuffer<NormalizedUpdate, 4096> normalized_buffer;
  
  // Latency tracking
  LatencyTracker raw_to_normalized_latency;
  LatencyTracker processing_latency;
  
  // Initialize Binance client
  BinanceClient client([&](const std::string& message) {
    auto now = std::chrono::high_resolution_clock::now();
    auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();
    
    MarketUpdate update;
    update.timestamp_ns = ts;
    update.raw_data = nlohmann::json::parse(message);
    update.event_type = update.raw_data["e"].get<std::string>();
    update.symbol = update.raw_data["s"].get<std::string>();
    
    raw_buffer.TryPush(update);
  });
  
  // Normalization thread
  std::thread normalize_thread([&]() {
    ThreadUtils::PinToCore(1);  // Pin to specific CPU core
    
    Normalizer normalizer;
    MarketUpdate raw;
    
    while (true) {
      if (raw_buffer.TryPop(&raw)) {
        auto start = std::chrono::high_resolution_clock::now();
        
        auto normalized = normalizer.Normalize(raw);
        normalized_buffer.TryPush(normalized);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start);
        
        raw_to_normalized_latency.RecordLatency(latency.count());
      }
    }
  });
  
  // Processing thread (e.g. order book updates)
  std::thread processing_thread([&]() {
    ThreadUtils::PinToCore(2);
    
    OrderBook order_book;
    NormalizedUpdate update;
    
    while (true) {
      if (normalized_buffer.TryPop(&update)) {
        auto start = std::chrono::high_resolution_clock::now();
        
        order_book.ProcessUpdate(update);
        
        auto end = std::chrono::high_resolution_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
            end - start);
        
        processing_latency.RecordLatency(latency.count());
      }
    }
  });
  
  // Statistics reporting thread
  std::thread stats_thread([&]() {
    while (true) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      
      std::cout << "Raw→Norm latency (ns): "
                << "min=" << raw_to_normalized_latency.MinLatency() 
                << " avg=" << raw_to_normalized_latency.AvgLatency()
                << " max=" << raw_to_normalized_latency.MaxLatency()
                << " p99=" << raw_to_normalized_latency.PercentileLatency(99)
                << std::endl;
                
      std::cout << "Processing latency (ns): "
                << "min=" << processing_latency.MinLatency() 
                << " avg=" << processing_latency.AvgLatency()
                << " max=" << processing_latency.MaxLatency()
                << " p99=" << processing_latency.PercentileLatency(99)
                << std::endl;
    }
  });
  
  // Connect to Binance
  client.Connect({"btcusdt@depth", "ethusdt@depth"});
  
  // Wait for threads (or implement proper shutdown)
  normalize_thread.join();
  processing_thread.join();
  stats_thread.join();
  
  return 0;
}