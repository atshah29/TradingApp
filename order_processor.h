#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

enum class OrderSide { Buy, Sell };
enum class OrderStatus { Pending, Processing, Filled, Rejected, Failed };

struct Order {
    std::uint64_t id;
    int userID;
    std::string symbol;
    int quantity;
    OrderSide side;
};

struct OrderResult {
    Order order;
    OrderStatus status = OrderStatus::Pending;
    std::string reason;
    std::chrono::steady_clock::time_point submitted;
    std::chrono::steady_clock::time_point started;
    std::chrono::steady_clock::time_point finished;
};

class OrderRejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

const char* statusName(OrderStatus status);
bool validStockSymbol(const std::string& symbol);

class OrderProcessor {
public:
    using Executor = std::function<void(const Order&)>;
    using ExecutorFactory = std::function<Executor()>;

    OrderProcessor(std::size_t workerCount, ExecutorFactory factory);
    ~OrderProcessor();
    OrderProcessor(const OrderProcessor&) = delete;
    OrderProcessor& operator=(const OrderProcessor&) = delete;

    std::uint64_t submit(int userID, std::string symbol, int quantity, OrderSide side);
    std::vector<OrderResult> ordersFor(int userID) const;
    void waitIdle();
    void shutdown();

private:
    void work(ExecutorFactory factory, std::promise<void> ready);

    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::condition_variable idle_;
    std::queue<std::uint64_t> queue_;
    std::map<std::uint64_t, OrderResult> results_;
    std::vector<std::thread> workers_;
    std::once_flag shutdownOnce_;
    std::uint64_t nextID_ = 1;
    std::size_t active_ = 0;
    bool stopping_ = false;
};
