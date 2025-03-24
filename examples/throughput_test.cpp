#include <cstdlib>
#include <functional>
#include <print>
#include <chrono>
#include <thread>

#include "tpl.hpp"

constexpr int NUM_PRODUCERS = 12;
constexpr int NUM_CONSUMERS = 12;
constexpr int TEST_DURATION_SECONDS = 5;
using queue_t = tpl::Queue<int, 128>;

std::atomic<bool> running{true};
std::atomic<size_t> push_count{0};
std::atomic<size_t> pop_count{0};

void producer(queue_t& queue) {
    while (running.load(std::memory_order_relaxed)) {
        if (queue.push(rand())) {
            push_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void consumer(queue_t& queue) {
    while (running.load(std::memory_order_relaxed)) {
        auto val = queue.pop();
        if (val) {
            pop_count.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

int main() {

    queue_t queue;  // Replace with your actual queue type

    std::vector<std::thread> threads;

    // Start producer threads
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        threads.emplace_back(producer, std::ref(queue));
    }

    // Start consumer threads
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        threads.emplace_back(consumer, std::ref(queue));
    }

    // Run the test for the given duration
    std::this_thread::sleep_for(std::chrono::seconds(TEST_DURATION_SECONDS));
    running.store(false, std::memory_order_relaxed);

    // Join all threads
    for (auto& t : threads) {
        t.join();
    }

    // Print throughput results
    std::println("Push Throughput: {} ops/sec", push_count / TEST_DURATION_SECONDS);
    std::println("Pop Throughput: {} ops/sec", pop_count / TEST_DURATION_SECONDS);
    return 0; 
}
