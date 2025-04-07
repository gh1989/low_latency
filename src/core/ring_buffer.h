// src/core/ring_buffer.h
#pragma once

#include <array>       // Add this include for std::array
#include <atomic>

template <typename T, size_t Size>
class LockFreeRingBuffer {
 public:
  bool TryPush(const T& item) {
    const size_t current_write = write_idx_.load(std::memory_order_relaxed);
    const size_t next_write = (current_write + 1) % Size;
    
    if (next_write == read_idx_.load(std::memory_order_acquire)) {
      return false;  // Buffer full
    }
    
    buffer_[current_write] = item;
    write_idx_.store(next_write, std::memory_order_release);
    return true;
  }
  
  bool TryPop(T* output) {
    const size_t current_read = read_idx_.load(std::memory_order_relaxed);
    
    if (current_read == write_idx_.load(std::memory_order_acquire)) {
      return false;  // Buffer empty
    }
    
    *output = std::move(buffer_[current_read]);
    read_idx_.store((current_read + 1) % Size, std::memory_order_release);
    return true;
  }
  
 private:
  alignas(64) std::atomic<size_t> write_idx_{0};  // Cache line alignment
  alignas(64) std::atomic<size_t> read_idx_{0};
  std::array<T, Size> buffer_;
};