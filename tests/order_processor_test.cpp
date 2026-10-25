#include "order_processor.h"

#include <atomic>
#include <iostream>
#include <set>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class F> void mustThrow(F action) {
    bool threw = false;
    try { action(); } catch (const std::exception&) { threw = true; }
    check(threw, "Expected an exception");
}

void concurrentProducers() {
    std::mutex mutex;
    std::set<std::uint64_t> executed;
    std::atomic<int> factories{0};
    std::atomic<bool> duplicate{false};
    OrderProcessor processor(4, [&] {
        ++factories;
        const auto owner = std::this_thread::get_id();
        return [&, owner](const Order& order) {
            check(owner == std::this_thread::get_id(), "Executor changed threads");
            std::lock_guard<std::mutex> lock(mutex);
            if (!executed.insert(order.id).second) duplicate = true;
        };
    });
    std::vector<std::thread> producers;
    for (int user = 1; user <= 4; ++user) {
        producers.emplace_back([&, user] {
            for (int i = 0; i < 250; ++i) processor.submit(user, "TEST", 1, OrderSide::Buy);
        });
    }
    for (auto& producer : producers) producer.join();
    processor.shutdown();
    processor.shutdown();
    check(factories == 4 && executed.size() == 1000 && !duplicate, "Lost or duplicate orders");
    for (int user = 1; user <= 4; ++user) {
        const auto results = processor.ordersFor(user);
        check(results.size() == 250, "Order ownership filter failed");
        for (const auto& result : results) {
            check(result.status == OrderStatus::Filled, "Execution failed");
            check(result.submitted <= result.started && result.started <= result.finished, "Invalid timestamps");
        }
    }
    check(processor.ordersFor(99).empty(), "Exposed another user's orders");
    mustThrow([&] { processor.submit(1, "TEST", 1, OrderSide::Buy); });
}

void statusesAndFailures() {
    std::promise<void> release, started;
    auto gate = release.get_future().share();
    auto running = started.get_future();
    OrderProcessor processor(1, [&] {
        return [&, gate](const Order& order) {
            if (order.symbol == "WAIT") { started.set_value(); gate.wait(); }
            if (order.symbol == "REJECT") throw OrderRejected("Insufficient funds.");
            if (order.symbol == "FAIL") throw std::runtime_error("Database disconnected.");
            if (order.symbol == "OTHER") throw 42;
        };
    });
    mustThrow([&] { processor.submit(1, "TEST", 0, OrderSide::Buy); });
    mustThrow([&] { processor.submit(0, "TEST", 1, OrderSide::Buy); });
    mustThrow([&] { processor.submit(1, "TEST;pwd", 1, OrderSide::Buy); });
    mustThrow([&] { processor.submit(1, "TEST", 1, static_cast<OrderSide>(9)); });
    processor.submit(1, "WAIT", 1, OrderSide::Buy);
    const bool woke = running.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    processor.submit(1, "REJECT", 1, OrderSide::Sell);
    const auto snapshot = processor.ordersFor(1);
    release.set_value();
    check(woke, "Sleeping worker did not wake");
    check(snapshot[0].status == OrderStatus::Processing && snapshot[1].status == OrderStatus::Pending,
          "Pending/processing status failed");
    processor.submit(1, "FAIL", 1, OrderSide::Buy);
    processor.submit(1, "OTHER", 1, OrderSide::Buy);
    processor.submit(1, "TEST", 1, OrderSide::Buy);
    processor.waitIdle();
    const auto results = processor.ordersFor(1);
    check(results[1].status == OrderStatus::Rejected && results[1].reason == "Insufficient funds.", "Missing rejection");
    check(results[2].status == OrderStatus::Failed && !results[2].reason.empty(), "Missing failure");
    check(results[3].status == OrderStatus::Failed, "Uncaught nonstandard exception");
    check(results[4].status == OrderStatus::Filled, "Worker stopped after an error");
}

void parallelExecution() {
    std::mutex mutex;
    std::condition_variable ready;
    int active = 0;
    std::promise<void> release;
    auto gate = release.get_future().share();
    OrderProcessor processor(4, [&] {
        return [&, gate](const Order&) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                ++active;
            }
            ready.notify_one();
            gate.wait();
        };
    });
    for (int i = 0; i < 4; ++i) processor.submit(i + 1, "TEST", 1, OrderSide::Buy);
    std::unique_lock<std::mutex> lock(mutex);
    const bool parallel = ready.wait_for(lock, std::chrono::seconds(5), [&] { return active == 4; });
    lock.unlock();
    release.set_value();
    processor.shutdown();
    check(parallel, "Queue lock was held during execution");
}

void shutdownRace() {
    std::atomic<int> executed{0}, accepted{0};
    OrderProcessor processor(4, [&] {
        return [&](const Order&) { ++executed; };
    });
    std::thread producer([&] {
        for (int i = 0; i < 1000; ++i) {
            try { processor.submit(1, "TEST", 1, OrderSide::Buy); ++accepted; }
            catch (const std::runtime_error&) { break; }
        }
    });
    std::thread closer([&] { processor.shutdown(); });
    processor.shutdown();
    closer.join();
    producer.join();
    check(executed == accepted, "Shutdown lost an accepted order");
}
}

int main() {
    try {
        concurrentProducers();
        statusesAndFailures();
        parallelExecution();
        shutdownRace();
        mustThrow([] { OrderProcessor processor(0, [] { return OrderProcessor::Executor{}; }); });
        mustThrow([] {
            std::atomic<int> count{0};
            OrderProcessor processor(4, [&]() -> OrderProcessor::Executor {
                if (++count == 2) throw std::runtime_error("Connection failed");
                return [](const Order&) {};
            });
        });
        std::cout << "Order processor tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
