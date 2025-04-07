// src/feed/binance_client.h
class BinanceClient {
 public:
  BinanceClient(
      const std::function<void(const std::string&)>& message_handler);
  
  // Connect to Binance and subscribe to market data
  bool Connect(const std::vector<std::string>& symbols);
  void Disconnect();
  
 private:
  std::function<void(const std::string&)> message_handler_;
  std::unique_ptr<WebSocketClient> ws_client_;  // WebSocket implementation
  
  void OnMessage(const std::string& msg);
  void Subscribe(const std::vector<std::string>& symbols);
};