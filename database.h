#pragma once

#include "mysqlx/xdevapi.h"
#include "order_processor.h"

#include <memory>
#include <string>
#include <vector>

class Database {
public:
    void connect(const std::string& url, const std::string& schemaName = "trading");
    int createUser(const std::string& username, const std::string& password);
    int loginUser(const std::string& username, const std::string& password);
    double getBalance(int userID);
    void depositMoney(int userID, double amount);
    void withdrawMoney(int userID, double amount);
    void buyStock(int userID, const std::string& symbol, int quantity);
    void sellStock(int userID, const std::string& symbol, int quantity);
    void executeOrder(const Order& order);
    void viewPortfolio(int userID);
    void viewTransactions(int userID);
    std::vector<std::string> returnStocks();
    static void updateStockPrices();
    static std::string getSentiment(const std::string& symbol, bool useTwitter);

private:
    void lockAccount(int userID);
    void changeBalance(int userID, double amount, bool deposit);
    std::unique_ptr<mysqlx::Session> session_;
};
