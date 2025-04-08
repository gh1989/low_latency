#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <iomanip>

namespace {

const int kIterations = 1'000'000;
const int kNumThreads = 24;

// ==================== Memory Ordering Example ====================

std::atomic<bool> ready{false};
std::atomic<int> data{0};

void producer_relaxed() {
    // With relaxed, the store to data and ready can be reordered
    data.store(42, std::memory_order_relaxed);
    ready.store(true, std::memory_order_relaxed);
}

void consumer_relaxed() {
    // With relaxed, we might see ready=true but data=0
    while (!ready.load(std::memory_order_relaxed)) {
        std::this_thread::yield();
    }
    int value = data.load(std::memory_order_relaxed);
    std::cout << "Relaxed consumer saw: " << value << std::endl;
}

void producer_release() {
    // With release, all prior writes are visible when ready becomes true
    data.store(42, std::memory_order_relaxed);
    ready.store(true, std::memory_order_release);
}

void consumer_acquire() {
    // With acquire, when we see ready=true, we see all writes before the release
    while (!ready.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    int value = data.load(std::memory_order_relaxed);
    std::cout << "Acquire-Release consumer saw: " << value << std::endl;
}

void reset_variables() {
    ready.store(false, std::memory_order_seq_cst);
    data.store(0, std::memory_order_seq_cst);
}

void run_memory_ordering_demo() {
    std::cout << "=== Memory Ordering Example ===" << std::endl;
    
    // Run each example just once instead of 5 times
    reset_variables();
    {
        std::thread t1(producer_relaxed);
        std::thread t2(consumer_relaxed);
        t1.join();
        t2.join();
    }
    
    reset_variables();
    {
        std::thread t1(producer_release);
        std::thread t2(consumer_acquire);
        t1.join();
        t2.join();
    }
    
    std::cout << std::endl;
}

// ==================== Cache Alignment Example ====================

// Structure with no alignment - prone to false sharing
struct UnalignedCounters {
    std::atomic<uint64_t> counters[kNumThreads];
};

// Structure with proper alignment - each counter on its own cache line
struct alignas(64) AlignedCounters {
    struct PaddedCounter {
        std::atomic<int64_t> value{0};
        // Padding to avoid false sharing (adjust size as needed)
        char padding[64 - sizeof(std::atomic<int64_t>)];
        
        // Allow direct conversion to the underlying value type for printing
        operator int64_t() const { 
            return value.load(std::memory_order_relaxed); 
        }
        
        // Add the fetch_add method that delegates to the atomic
        int64_t fetch_add(int64_t increment, std::memory_order order) {
            return value.fetch_add(increment, order);
        }
    };
    
    PaddedCounter counters[kNumThreads];
};

template <typename T>
void increment_counter(T& counters, int thread_id) {
    // Loop unrolling for better performance
    for (int i = 0; i < kIterations; i += 8) {
        // Increment 8 times per loop iteration
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
    }
    
    // Handle any remaining iterations
    for (int i = (kIterations / 8) * 8; i < kIterations; i++) {
        counters.counters[thread_id].fetch_add(1, std::memory_order_relaxed);
    }
}

template <typename T>
void run_counter_benchmark(const char* name) {
    T counters{};
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; i++) {
        threads.emplace_back(increment_counter<T>, std::ref(counters), i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << name << " took " << duration.count() << " ms" << std::endl;
    
    // Verify all counters have correct values
    for (int i = 0; i < kNumThreads; i++) {
        std::cout << "  Counter " << i << ": " << counters.counters[i] << std::endl;
    }
}

template <typename T>
int64_t run_counter_benchmark_for_scaling(int num_threads) {
    T counters{};
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; i++) {
        threads.emplace_back(increment_counter<T>, std::ref(counters), i);
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    // Verify all counters have correct values (without printing)
    for (int i = 0; i < num_threads; i++) {
        if (counters.counters[i] != kIterations) {
            std::cerr << "Error: Counter " << i << " expected " << kIterations 
                      << " but got " << counters.counters[i] << std::endl;
        }
    }
    
    return duration.count();
}

void run_alignment_demo() {
    std::cout << "=== Cache Alignment Example ===" << std::endl;
    std::cout << "Running with " << kNumThreads << " threads, " 
              << kIterations << " iterations each" << std::endl;
    std::cout << "Size of UnalignedCounters: " << sizeof(UnalignedCounters) << " bytes" << std::endl;
    std::cout << "Size of AlignedCounters: " << sizeof(AlignedCounters) << " bytes" << std::endl;
    
    std::cout << "\nRunning unaligned benchmark (expect false sharing slowdown):" << std::endl;
    run_counter_benchmark<UnalignedCounters>("Unaligned counters");
    
    std::cout << "\nRunning aligned benchmark (expect better performance):" << std::endl;
    run_counter_benchmark<AlignedCounters>("Aligned counters");
}

void run_thread_scaling_test() {
    std::cout << "=== Thread Scaling Test ===" << std::endl;
    std::cout << "This test shows how performance scales with thread count" << std::endl << std::endl;
    
    // Run fewer thread configurations
    for (int num_threads : {1, 4, 24}) {
        if (num_threads > static_cast<int>(std::thread::hardware_concurrency())) {
            std::cout << "Skipping " << num_threads << " threads (exceeds hardware support of " 
                      << std::thread::hardware_concurrency() << " cores)" << std::endl;
            continue;
        }
        
        // Test both implementations
        std::cout << "Testing with " << num_threads << " threads:" << std::endl;
        
        // Skip tests that would exceed counter array size
        if (num_threads > kNumThreads) {
            std::cout << "  Skipping test (exceeds counter array size of " << kNumThreads << ")" << std::endl;
            std::cout << "  To test with more threads, increase kNumThreads at the top of the file" << std::endl;
            std::cout << std::endl;
            continue;
        }
        
        auto unaligned_ms = run_counter_benchmark_for_scaling<UnalignedCounters>(num_threads);
        auto aligned_ms = run_counter_benchmark_for_scaling<AlignedCounters>(num_threads);
        
        // Calculate the actual slowdown factor - fixed calculation
        double slowdown = static_cast<double>(unaligned_ms) / static_cast<double>(aligned_ms);
        
        std::cout << "  Unaligned took " << unaligned_ms << " ms" << std::endl;
        std::cout << "  Aligned took " << aligned_ms << " ms" << std::endl;
        std::cout << "  Slowdown factor: " << std::fixed << std::setprecision(1) << slowdown << "x" << std::endl;
        
        std::cout << std::endl;
    }
    
    std::cout << "\nPerformance Analysis:" << std::endl;
    std::cout << "1. If unaligned performance degrades much faster than aligned as threads increase," << std::endl;
    std::cout << "   this confirms false sharing is occurring." << std::endl;
    std::cout << "2. The aligned version should scale almost linearly with cores." << std::endl;
    std::cout << "3. Your 24-core system should show dramatic differences at higher thread counts!" << std::endl;
}

} // namespace

int main() {
    // Memory ordering example
    run_memory_ordering_demo();
    
    // Cache alignment example
    run_alignment_demo();
    
    // Thread scaling test (new)
    run_thread_scaling_test();
    
    return 0;
} 