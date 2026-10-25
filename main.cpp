#include "database.h"
#include "order_processor.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace {
class EndOfInput {};

template<class T>
T read(const char* prompt) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) throw EndOfInput{};
    std::istringstream input(line);
    T value;
    if (!(input >> value)) throw std::invalid_argument("Invalid input.");
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("Unexpected text after input.");
    return value;
}

class MarketData {
public:
    MarketData(std::vector<std::string> symbols, bool enabled) {
        if (!enabled) return;
        worker_ = std::thread([this, symbols = std::move(symbols)] {
            try { Database::updateStockPrices(); }
            catch (const std::exception& error) { setError(error.what()); }
            while (true) {
                for (const auto& symbol : symbols) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (stopping_) return;
                    }
                    try {
                        auto value = Database::getSentiment(symbol, false);
                        std::lock_guard<std::mutex> lock(mutex_);
                        cache_[symbol] = std::move(value);
                    } catch (const std::exception& error) { setError(error.what()); }
                }
                std::unique_lock<std::mutex> lock(mutex_);
                if (wake_.wait_for(lock, std::chrono::minutes(2), [this] { return stopping_; })) return;
            }
        });
    }
    ~MarketData() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    std::string sentiment(const std::string& symbol, bool twitter) {
        if (!twitter) {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = cache_.find(symbol);
            if (found != cache_.end()) return "Cached: " + found->second;
        }
        auto value = Database::getSentiment(symbol, twitter);
        if (!twitter) {
            std::lock_guard<std::mutex> lock(mutex_);
            cache_[symbol] = value;
        }
        return value;
    }
    std::string lastError() {
        std::lock_guard<std::mutex> lock(mutex_);
        return error_;
    }
private:
    void setError(std::string error) {
        std::lock_guard<std::mutex> lock(mutex_);
        error_ = std::move(error);
    }
    std::mutex mutex_;
    std::condition_variable wake_;
    std::unordered_map<std::string, std::string> cache_;
    std::string error_;
    bool stopping_ = false;
    std::thread worker_;
};

void showOrders(const OrderProcessor& orders, int userID) {
    const auto results = orders.ordersFor(userID);
    if (results.empty()) std::cout << "No orders submitted in this session.\n";
    for (const auto& result : results) {
        std::cout << '#' << result.order.id << ' '
                  << (result.order.side == OrderSide::Buy ? "Buy " : "Sell ")
                  << result.order.quantity << ' ' << result.order.symbol
                  << " | " << statusName(result.status);
        if (!result.reason.empty()) std::cout << " | " << result.reason;
        if (result.finished != std::chrono::steady_clock::time_point{}) {
            std::cout << " | " << std::chrono::duration<double, std::milli>(
                result.finished - result.submitted).count() << " ms";
        }
        if (result.status == OrderStatus::Failed) {
            std::cout << " | Verify transaction history before resubmitting; the outcome may be unknown.";
        }
        std::cout << '\n';
    }
}

void userMenu(Database& db, OrderProcessor& orders, MarketData& market, int userID) {
    while (true) {
        try {
            const int choice = read<int>(
                "\n1. Deposit money\n2. Buy stock\n3. Sell stock\n4. View portfolio\n"
                "5. View transactions\n6. Stock sentiment\n7. Logout\n8. Order status\nChoice: ");
            if (choice == 1) {
                db.depositMoney(userID, read<double>("Amount: "));
                std::cout << "Deposit complete.\n";
            } else if (choice == 2 || choice == 3) {
                const auto symbol = read<std::string>("Stock symbol (uppercase): ");
                const int quantity = read<int>("Quantity: ");
                const auto id = orders.submit(userID, symbol, quantity,
                    choice == 2 ? OrderSide::Buy : OrderSide::Sell);
                std::cout << "Order #" << id << " submitted. Choose 8 to check its status.\n";
            } else if (choice == 4) {
                db.viewPortfolio(userID);
            } else if (choice == 5) {
                db.viewTransactions(userID);
            } else if (choice == 6) {
                const auto symbol = read<std::string>("Stock symbol (uppercase): ");
                const int twitter = read<int>("Include Twitter/X? (1=yes, 0=no): ");
                if (twitter != 0 && twitter != 1) throw std::invalid_argument("Enter 0 or 1.");
                const auto error = market.lastError();
                if (!error.empty()) std::cout << "Last background update error: " << error << '\n';
                std::cout << market.sentiment(symbol, twitter == 1);
            } else if (choice == 7) {
                return;
            } else if (choice == 8) {
                showOrders(orders, userID);
            } else {
                std::cout << "Invalid choice.\n";
            }
        } catch (const std::exception& error) {
            std::cout << "Error: " << error.what() << '\n';
        }
    }
}
}

int main(int argc, char** argv) {
    try {
        std::size_t workerCount = 4;
        bool marketEnabled = true;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--no-market-data") marketEnabled = false;
            else if (arg == "--workers" && i + 1 < argc) {
                const std::string count = argv[++i];
                std::size_t end;
                workerCount = std::stoul(count, &end);
                if (end != count.size() || workerCount == 0 || workerCount > 64) {
                    throw std::invalid_argument("Worker count must be between 1 and 64.");
                }
            } else {
                throw std::invalid_argument("Usage: TradingApp [--workers 1..64] [--no-market-data]");
            }
        }
        const auto url = read<std::string>("MySQL X connection URL (mysqlx://user:password@localhost:33060): ");
        Database db;
        db.connect(url);
        OrderProcessor orders(workerCount, [url] {
            auto connection = std::make_shared<Database>();
            connection->connect(url);
            return [connection](const Order& order) { connection->executeOrder(order); };
        });
        MarketData market(db.returnStocks(), marketEnabled);
        std::cout << workerCount << " order workers ready.\n";
        try {
            while (true) {
                try {
                    const int choice = read<int>("\n1. Login\n2. Create user\n3. Exit\nChoice: ");
                    if (choice == 3) break;
                    if (choice != 1 && choice != 2) { std::cout << "Invalid choice.\n"; continue; }
                    const auto username = read<std::string>("Username: ");
                    const auto password = read<std::string>("Password: ");
                    if (choice == 2) {
                        std::cout << "Created user #" << db.createUser(username, password) << '\n';
                    } else {
                        userMenu(db, orders, market, db.loginUser(username, password));
                    }
                } catch (const std::exception& error) {
                    std::cout << "Error: " << error.what() << '\n';
                }
            }
        } catch (const EndOfInput&) {}
        std::cout << "Finishing accepted orders and stopping background work...\n";
        orders.shutdown();
    } catch (const EndOfInput&) {
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
