#include "order_processor.h"

#include <algorithm>
#include <utility>

const char* statusName(OrderStatus status) {
    switch (status) {
        case OrderStatus::Pending: return "Pending";
        case OrderStatus::Processing: return "Processing";
        case OrderStatus::Filled: return "Filled";
        case OrderStatus::Rejected: return "Rejected";
        case OrderStatus::Failed: return "Failed";
    }
    return "Unknown";
}

bool validStockSymbol(const std::string& symbol) {
    return !symbol.empty() && symbol.size() <= 16 &&
        std::all_of(symbol.begin(), symbol.end(), [](unsigned char ch) {
            return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                   ch == '.' || ch == '-';
        });
}

OrderProcessor::OrderProcessor(std::size_t workerCount, ExecutorFactory factory) {
    if (workerCount == 0 || !factory) {
        throw std::invalid_argument("At least one worker and an executor factory are required.");
    }
    std::vector<std::future<void>> ready;
    try {
        for (std::size_t i = 0; i < workerCount; ++i) {
            std::promise<void> started;
            ready.push_back(started.get_future());
            workers_.emplace_back(&OrderProcessor::work, this, factory, std::move(started));
        }
        for (auto& started : ready) started.get();
    } catch (...) {
        shutdown();
        throw;
    }
}

OrderProcessor::~OrderProcessor() {
    shutdown();
}

std::uint64_t OrderProcessor::submit(int userID, std::string symbol, int quantity,
                                   OrderSide side) {
    if (userID <= 0 || quantity <= 0 || !validStockSymbol(symbol) ||
        (side != OrderSide::Buy && side != OrderSide::Sell)) {
        throw std::invalid_argument("Use a valid user, uppercase stock symbol, and positive quantity.");
    }
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) throw std::runtime_error("Order processing is shutting down.");
        id = nextID_++;
        OrderResult result{};
        result.order = {id, userID, std::move(symbol), quantity, side};
        result.submitted = std::chrono::steady_clock::now();
        results_.emplace(id, std::move(result));
        try {
            queue_.push(id);
        } catch (...) {
            results_.erase(id);
            throw;
        }
    }
    available_.notify_one();
    return id;
}

std::vector<OrderResult> OrderProcessor::ordersFor(int userID) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OrderResult> result;
    for (const auto& entry : results_) {
        if (entry.second.order.userID == userID) result.push_back(entry.second);
    }
    return result;
}

void OrderProcessor::waitIdle() {
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] { return queue_.empty() && active_ == 0; });
}

void OrderProcessor::shutdown() {
    std::call_once(shutdownOnce_, [this] {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        available_.notify_all();
        for (auto& worker : workers_) worker.join();
    });
}

void OrderProcessor::work(ExecutorFactory factory, std::promise<void> ready) {
    Executor execute;
    try {
        execute = factory();
        if (!execute) throw std::runtime_error("Worker executor is empty.");
        ready.set_value();
    } catch (...) {
        ready.set_exception(std::current_exception());
        return;
    }

    while (true) {
        Order order;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) return;
            auto& result = results_.at(queue_.front());
            order = result.order;
            queue_.pop();
            ++active_;
            result.status = OrderStatus::Processing;
            result.started = std::chrono::steady_clock::now();
        }

        OrderStatus status = OrderStatus::Filled;
        std::string reason;
        try {
            execute(order);
        } catch (const OrderRejected& error) {
            status = OrderStatus::Rejected;
            reason = error.what();
        } catch (const std::exception& error) {
            status = OrderStatus::Failed;
            reason = error.what();
        } catch (...) {
            status = OrderStatus::Failed;
            reason = "Unknown execution error.";
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto& result = results_.at(order.id);
            result.status = status;
            result.reason = std::move(reason);
            result.finished = std::chrono::steady_clock::now();
            --active_;
        }
        idle_.notify_all();
    }
}
