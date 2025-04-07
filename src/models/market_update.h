// src/models/market_update.h
struct MarketUpdate {
  uint64_t timestamp_ns;          // Timestamp in nanoseconds
  std::string symbol;             // Trading pair (e.g., "BTCUSDT")
  std::string event_type;         // Binance event type
  nlohmann::json raw_data;        // Full JSON data
};

struct NormalizedUpdate {
  enum class Type { TRADE, BID, ASK };
  
  uint64_t exchange_ts;           // Exchange timestamp
  uint64_t received_ts;           // Local received timestamp
  std::string symbol;
  Type type;
  double price;
  double quantity;
  uint64_t update_id;            // Binance specific sequence number
};