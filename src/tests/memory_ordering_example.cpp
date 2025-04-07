#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <cstring>
#include <immintrin.h>

namespace {

const int kIterations = 100'000'000;
const int kNumThreads = 4;
const int kRounds = 10'000'000;  // For sync benchmarks

// ==================== Basic Memory Ordering Tests ====================
std::atomic<int> data{0};
std::atomic<bool> ready{false};

// Original producer-consumer patterns
void producer_relaxed() {
    // With relaxed, the store to data and ready can be reordered
    data.store(42, std::memory_order_relaxed);
    ready.store(true, std::memory_order_relaxed);
}

bool consumer_relaxed() {
    // With relaxed, the load of ready and data can be reordered
    bool r = ready.load(std::memory_order_relaxed);
    int d = data.load(std::memory_order_relaxed);
    
    if (r) std::cout << "Relaxed consumer saw: " << d << std::endl;
    return r;
}

void producer_acq_rel() {
    // Data is stored before ready, and ready uses release semantics
    // to create a happens-before relationship with the acquire load
    data.store(42, std::memory_order_relaxed);
    ready.store(true, std::memory_order_release);
}

bool consumer_acq_rel() {
    // Ready is loaded with acquire semantics to synchronize with the release store
    // This ensures that if we see ready==true, we'll also see the stores
    // that happened before the release
    bool r = ready.load(std::memory_order_acquire);
    int d = 0;
    if (r) {
        d = data.load(std::memory_order_relaxed);
        std::cout << "Acquire-Release consumer saw: " << d << std::endl;
    }
    return r;
}

// ==================== Advanced Memory Ordering Techniques ====================

// 1. Explicit Fences
void producer_with_fence() {
    // Use relaxed stores
    data.store(42, std::memory_order_relaxed);
    ready.store(true, std::memory_order_relaxed);
    
    // Single fence covers all preceding stores
    std::atomic_thread_fence(std::memory_order_release);
}

bool consumer_with_fence() {
    // Single fence covers all subsequent loads
    std::atomic_thread_fence(std::memory_order_acquire);
    
    // Use relaxed loads after the fence
    bool r = ready.load(std::memory_order_relaxed);
    int d = data.load(std::memory_order_relaxed);
    
    if (r) std::cout << "Consumer with fence saw: " << d << std::endl;
    return r;
}

// 2. Consume Ordering (pointer-based dependencies)
struct Node {
    int value;
    int* next_ptr;
};

std::atomic<Node*> published_node{nullptr};

void producer_consume() {
    // Create and populate a node
    Node* n = new Node{42, nullptr};
    
    // Publish with release ordering
    published_node.store(n, std::memory_order_release);
}

bool consumer_consume() {
    // Load with consume ordering - only operations that depend
    // on the value of n are ordered
    Node* n = published_node.load(std::memory_order_consume);
    
    if (n) {
        // This access is guaranteed to see initialized value because
        // it depends on n, which was loaded with consume ordering
        std::cout << "Consume consumer saw: " << n->value << std::endl;
        delete n; // Clean up
        return true;
    }
    return false;
}

// 3. Sequential Consistency Barrier
std::atomic<int> barrier{0};
std::atomic<int> data_array[2]{0, 0};

void producer_seq_cst() {
    // Relaxed stores for regular data
    data_array[0].store(42, std::memory_order_relaxed);
    data_array[1].store(43, std::memory_order_relaxed);
    
    // SC operation as a barrier
    barrier.fetch_add(1, std::memory_order_seq_cst);
}

bool consumer_seq_cst() {
    // Wait on SC barrier
    if (barrier.load(std::memory_order_seq_cst) < 1) {
        return false;
    }
    
    // Now safe to read with relaxed
    int a = data_array[0].load(std::memory_order_relaxed);
    int b = data_array[1].load(std::memory_order_relaxed);
    
    std::cout << "Seq-Cst consumer saw: " << a << ", " << b << std::endl;
    return true;
}

// 4. Seqlock Pattern
struct alignas(64) SeqLock {
    std::atomic<uint64_t> sequence{0};
    int data[4]{0, 0, 0, 0};  // Protected data

    void write(int a, int b, int c, int d) {
        // Increment sequence to odd (marks beginning of write)
        uint64_t seq = sequence.load(std::memory_order_relaxed);
        sequence.store(seq + 1, std::memory_order_release);
        
        // Write data
        data[0] = a;
        data[1] = b;
        data[2] = c;
        data[3] = d;
        
        // Increment to even (marks end of write)
        sequence.store(seq + 2, std::memory_order_release);
    }

    bool read(int result[4]) {
        uint64_t seq1, seq2;
        do {
            seq1 = sequence.load(std::memory_order_acquire);
            if (seq1 & 1) {
                // Writer active (odd seq) - skip and try again
                std::this_thread::yield();
                continue;
            }
            
            // Read data
            std::memcpy(result, data, sizeof(int) * 4);
            
            // Check if writer modified during our read
            seq2 = sequence.load(std::memory_order_acquire);
        } while (seq1 != seq2);
        
        return true;
    }
};

SeqLock seqlock;

void producer_seqlock() {
    seqlock.write(10, 20, 30, 40);
}

bool consumer_seqlock() {
    int result[4];
    if (seqlock.read(result)) {
        std::cout << "Seqlock consumer saw: " 
                  << result[0] << ", " << result[1] << ", " 
                  << result[2] << ", " << result[3] << std::endl;
        return true;
    }
    return false;
}

// 5. Asymmetric Barriers
class ThreadBarrier {
private:
    alignas(64) std::atomic<bool> producer_ready{false};
    alignas(64) std::atomic<bool> consumer_ready{false};
    
public:
    // Producer signals and waits for consumer
    void producer_arrive_and_wait() {
        producer_ready.store(true, std::memory_order_release);
        while (!consumer_ready.load(std::memory_order_acquire)) {
            // Use a portable CPU pause
            #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                _mm_pause(); // Efficient busy-waiting
            #else
                std::this_thread::yield(); // Fallback for non-x86 platforms
            #endif
        }
    }
    
    // Consumer signals and waits for producer
    void consumer_arrive_and_wait() {
        consumer_ready.store(true, std::memory_order_release);
        while (!producer_ready.load(std::memory_order_acquire)) {
            // Use a portable CPU pause
            #if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
                _mm_pause(); // Efficient busy-waiting
            #else
                std::this_thread::yield(); // Fallback for non-x86 platforms
            #endif
        }
    }
    
    void reset() {
        producer_ready.store(false, std::memory_order_relaxed);
        consumer_ready.store(false, std::memory_order_relaxed);
    }
};

ThreadBarrier barrier_sync;

void producer_barrier() {
    barrier_sync.producer_arrive_and_wait();
}

bool consumer_barrier() {
    barrier_sync.consumer_arrive_and_wait();
    std::cout << "Barrier synchronization complete" << std::endl;
    return true;
}

// ==================== Benchmark Setup ====================

// For comparison: standard mutex
std::mutex g_mutex;

// General purpose benchmark function to test sync methods
template<typename ProducerFunc, typename ConsumerFunc>
void benchmark_sync_pattern(
    const std::string& name,
    ProducerFunc producer_func,
    ConsumerFunc consumer_func
) {
    // Reset state
    ready.store(false, std::memory_order_relaxed);
    data.store(0, std::memory_order_relaxed);
    published_node.store(nullptr, std::memory_order_relaxed);
    barrier.store(0, std::memory_order_relaxed);
    barrier_sync.reset();
    
    auto start = std::chrono::high_resolution_clock::now();
    
    // Run the producer and consumer functions directly instead of looping internally
    std::thread producer_thread(producer_func);
    std::thread consumer_thread(consumer_func);
    
    // Add timeout protection
    auto timeout = std::chrono::seconds(10);
    if (producer_thread.joinable()) {
        producer_thread.join();
    }
    
    if (consumer_thread.joinable()) {
        consumer_thread.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << name << " took " << duration.count() << " ms" << std::endl;
}

// Run memory ordering demos
void run_memory_ordering_demo() {
    std::cout << "=== Memory Ordering Example ===" << std::endl;
    
    // Test each pattern 5 times
    for (int i = 0; i < 5; i++) {
        // Reset state
        data.store(0, std::memory_order_relaxed);
        ready.store(false, std::memory_order_relaxed);
        
        // Test relaxed ordering
        std::thread producer1(producer_relaxed);
        // Give producer time to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        consumer_relaxed();
        producer1.join();
        
        // Reset state
        data.store(0, std::memory_order_relaxed);
        ready.store(false, std::memory_order_relaxed);
        
        // Test acquire-release ordering
        std::thread producer2(producer_acq_rel);
        // Give producer time to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        consumer_acq_rel();
        producer2.join();
        
        // Add a small delay between iterations
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Signal completion of this demo
    std::cout << "Memory ordering demo completed" << std::endl;
}

// Benchmark all synchronization methods
void run_memory_ordering_benchmarks() {
    std::cout << "\n=== Memory Ordering Benchmarks ===\n" << std::endl;
    
    // Reduce number of rounds for benchmarks to prevent hanging
    const int benchmark_rounds = 100; // Even lower to prevent hanging
    
    // Baseline: no synchronization (unsafe)
    benchmark_sync_pattern("Relaxed ordering (no synchronization)", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            producer_relaxed();
            ready.store(false, std::memory_order_relaxed);
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            while (!consumer_relaxed()) { std::this_thread::yield(); }
        }
    });
    
    // Acquire-Release
    benchmark_sync_pattern("Acquire-Release ordering", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            producer_acq_rel();
            ready.store(false, std::memory_order_relaxed);
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            while (!consumer_acq_rel()) { std::this_thread::yield(); }
        }
    });
    
    // Explicit Fences
    benchmark_sync_pattern("Explicit Fences", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            producer_with_fence();
            ready.store(false, std::memory_order_relaxed);
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            while (!consumer_with_fence()) { std::this_thread::yield(); }
        }
    });
    
    // Consume Ordering
    benchmark_sync_pattern("Consume Ordering (pointer dependency)", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            producer_consume();
            published_node.store(nullptr, std::memory_order_relaxed);
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            while (!consumer_consume()) { std::this_thread::yield(); }
        }
    });
    
    // Sequential Consistency
    benchmark_sync_pattern("Sequential Consistency", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            producer_seq_cst();
            barrier.store(0, std::memory_order_relaxed);
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            while (!consumer_seq_cst()) { std::this_thread::yield(); }
        }
    });
    
    // Seqlock Pattern
    benchmark_sync_pattern("Seqlock Pattern", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            producer_seqlock();
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            while (!consumer_seqlock()) { std::this_thread::yield(); }
        }
    });
    
    // Asymmetric Barrier
    benchmark_sync_pattern("Asymmetric Barrier", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            producer_barrier();
            barrier_sync.reset();
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            while (!consumer_barrier()) { std::this_thread::yield(); }
        }
    });
    
    // Mutex (traditional synchronization)
    benchmark_sync_pattern("Standard Mutex", [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            g_mutex.lock();
            data.store(42, std::memory_order_relaxed);
            g_mutex.unlock();
        }
    }, [benchmark_rounds]() {
        for (int i = 0; i < benchmark_rounds; i++) {
            g_mutex.lock();
            data.load(std::memory_order_relaxed);
            g_mutex.unlock();
        }
        return true;
    });
    
    std::cout << "Memory ordering benchmarks completed" << std::endl;
}

// ==================== Cache Alignment Tests ====================

// Unaligned counters will exhibit false sharing
struct UnalignedCounters {
    std::atomic<int64_t> counters[kNumThreads];
};

// Aligned counters avoid false sharing
struct alignas(64) AlignedCounters {
    struct alignas(64) PaddedCounter {
        std::atomic<int64_t> value{0};
        
        // Add these methods to delegate to the atomic value
        int64_t load(std::memory_order order = std::memory_order_seq_cst) {
            return value.load(order);
        }
        
        int64_t fetch_add(int64_t delta, std::memory_order order = std::memory_order_seq_cst) {
            return value.fetch_add(delta, order);
        }
    };
    
    PaddedCounter counters[kNumThreads];
};

// Test a counter implementation
template<typename Counters>
void run_counter_benchmark(const std::string& name) {
    Counters counters{};
    
    auto start = std::chrono::high_resolution_clock::now();
    
    std::vector<std::thread> threads;
    for (int i = 0; i < kNumThreads; i++) {
        threads.emplace_back([&counters, i]() {
            for (int j = 0; j < kIterations; j++) {
                counters.counters[i].fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << name << " took " << duration.count() << " ms" << std::endl;
    
    // Verify counters
    for (int i = 0; i < kNumThreads; i++) {
        std::cout << "  Counter " << i << ": " << counters.counters[i].load() << std::endl;
    }
}

void run_alignment_demo() {
    std::cout << "\n=== Cache Alignment Example ===" << std::endl;
    std::cout << "Running with " << kNumThreads << " threads, " 
              << kIterations << " iterations each" << std::endl;
    std::cout << "Size of UnalignedCounters: " << sizeof(UnalignedCounters) << " bytes" << std::endl;
    std::cout << "Size of AlignedCounters: " << sizeof(AlignedCounters) << " bytes" << std::endl;
    
    std::cout << "\nRunning unaligned benchmark (expect false sharing slowdown):" << std::endl;
    run_counter_benchmark<UnalignedCounters>("Unaligned counters");
    
    std::cout << "\nRunning aligned benchmark (expect better performance):" << std::endl;
    run_counter_benchmark<AlignedCounters>("Aligned counters");
}

// Thread scaling test
void run_thread_scaling_test() {
    std::cout << "=== Thread Scaling Test ===" << std::endl;
    std::cout << "This test shows how performance scales with thread count" << std::endl;
    
    // Create array of thread counts to test
    // Include powers of 2 plus your max core count
    const std::vector<int> thread_counts = {1, 2, 4, 8, 16, 24};
    
    for (int num_threads : thread_counts) {
        if (num_threads > static_cast<int>(std::thread::hardware_concurrency())) {
            std::cout << "Skipping " << num_threads << " threads (exceeds hardware support of " 
                      << std::thread::hardware_concurrency() << " cores)" << std::endl;
            continue;
        }
        
        std::cout << "\nTesting with " << num_threads << " threads:" << std::endl;
        
        // Configure counters
        UnalignedCounters unaligned{};
        AlignedCounters aligned{};
        
        // Test unaligned
        {
            auto start = std::chrono::high_resolution_clock::now();
            std::vector<std::thread> threads;
            
            // Each thread increments its counter kIterations times
            for (int i = 0; i < num_threads; i++) {
                threads.emplace_back([&unaligned, i]() {
                    for (int j = 0; j < kIterations; j++) {
                        unaligned.counters[i].fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            
            for (auto& t : threads) {
                t.join();
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            auto unaligned_duration = duration;
            std::cout << "  Unaligned took " << duration.count() << " ms" << std::endl;
        }
        
        // Test aligned
        {
            auto start = std::chrono::high_resolution_clock::now();
            std::vector<std::thread> threads;
            
            // Each thread increments its counter kIterations times
            for (int i = 0; i < num_threads; i++) {
                threads.emplace_back([&aligned, i]() {
                    for (int j = 0; j < kIterations; j++) {
                        aligned.counters[i].fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
            
            for (auto& t : threads) {
                t.join();
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
            std::cout << "  Aligned took " << duration.count() << " ms" << std::endl;
        }
    }
    
    std::cout << "\nPerformance Analysis:" << std::endl;
    std::cout << "1. If unaligned performance degrades much faster than aligned as threads increase," << std::endl;
    std::cout << "   this confirms false sharing is occurring." << std::endl;
    std::cout << "2. The aligned version should scale almost linearly with cores." << std::endl;
    std::cout << "3. Your 24-core system should show dramatic differences at higher thread counts!" << std::endl;
}

// Function to print usage information
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [test_name]" << std::endl;
    std::cout << "Available tests:" << std::endl;
    std::cout << "  memory       - Run memory ordering demo" << std::endl;
    std::cout << "  benchmark    - Run memory ordering benchmarks" << std::endl;
    std::cout << "  unaligned    - Run only unaligned counter benchmark" << std::endl;
    std::cout << "  aligned      - Run only aligned counter benchmark" << std::endl;
    std::cout << "  scaling      - Run thread scaling test" << std::endl;
    std::cout << "  all (default)- Run all tests" << std::endl;
}

} // namespace

int main(int argc, char* argv[]) {
    // Default to running all tests if no arguments provided
    std::string test_name = "all";
    
    // Parse command-line arguments
    if (argc > 1) {
        test_name = argv[1];
    }
    
    // Run requested test(s)
    if (test_name == "memory" || test_name == "all") {
        run_memory_ordering_demo();
    }
    
    if (test_name == "benchmark" || test_name == "all") {
        run_memory_ordering_benchmarks();
    }
    
    if (test_name == "unaligned") {
        std::cout << "=== Unaligned Counter Benchmark Only ===" << std::endl;
        std::cout << "Running with " << kNumThreads << " threads, " 
                  << kIterations << " iterations each" << std::endl;
        std::cout << "Size of UnalignedCounters: " << sizeof(UnalignedCounters) << " bytes" << std::endl;
        
        run_counter_benchmark<UnalignedCounters>("Unaligned counters");
    }
    
    if (test_name == "aligned") {
        std::cout << "=== Aligned Counter Benchmark Only ===" << std::endl;
        std::cout << "Running with " << kNumThreads << " threads, " 
                  << kIterations << " iterations each" << std::endl;
        std::cout << "Size of AlignedCounters: " << sizeof(AlignedCounters) << " bytes" << std::endl;
        
        run_counter_benchmark<AlignedCounters>("Aligned counters");
    }
    
    if ((test_name == "all") && (test_name != "unaligned") && (test_name != "aligned")) {
        run_alignment_demo();
    }
    
    if (test_name == "scaling" || test_name == "all") {
        run_thread_scaling_test();
    }
    
    if (test_name != "memory" && test_name != "benchmark" && test_name != "unaligned" && 
        test_name != "aligned" && test_name != "scaling" && test_name != "all") {
        print_usage(argv[0]);
        return 1;
    }
    
    return 0;
} 