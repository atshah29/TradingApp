#include "database.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
class Transaction {
public:
    explicit Transaction(mysqlx::Session& session) : session_(session) {
        session_.startTransaction();
    }
    ~Transaction() {
        if (!committed_) {
            try { session_.rollback(); } catch (...) {}
        }
    }
    void commit() {
        session_.commit();
        committed_ = true;
    }
private:
    mysqlx::Session& session_;
    bool committed_ = false;
};

std::string money(double amount) {
    if (!std::isfinite(amount) || amount < 0.0001 || amount >= 1e16) {
        throw OrderRejected("Amount must be positive and fit DECIMAL(20,4).");
    }
    std::ostringstream result;
    result.imbue(std::locale::classic());
    result << std::fixed << std::setprecision(4) << amount;
    return result.str();
}
}

void Database::connect(const std::string& url, const std::string& schemaName) {
    if (session_) throw std::logic_error("Database is already connected.");
    if (schemaName.empty() || !std::all_of(schemaName.begin(), schemaName.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    })) throw std::invalid_argument("Invalid schema name.");

    auto session = std::make_unique<mysqlx::Session>(url);
    session->sql("USE `" + schemaName + "`").execute();
    session->sql("SET SESSION TRANSACTION ISOLATION LEVEL READ COMMITTED").execute();
    session->sql("SET SESSION sql_mode=CONCAT_WS(',', @@sql_mode, 'STRICT_ALL_TABLES')").execute();
    auto tables = session->sql(
        "SELECT COUNT(*) FROM information_schema.tables WHERE TABLE_SCHEMA=? "
        "AND TABLE_NAME IN ('Users', 'Stocks', 'Transactions') AND ENGINE='InnoDB'")
        .bind(schemaName).execute().fetchOne();
    if (tables[0].get<int>() != 3) {
        throw std::runtime_error("Users, Stocks, and Transactions must exist and use InnoDB. See sql/schema.sql.");
    }
    auto moneyColumns = session->sql(
        "SELECT COUNT(*) FROM information_schema.columns WHERE TABLE_SCHEMA=? "
        "AND ((TABLE_NAME='Users' AND COLUMN_NAME='Balance') "
        "OR (TABLE_NAME='Stocks' AND COLUMN_NAME='StockPrice') "
        "OR (TABLE_NAME='Transactions' AND COLUMN_NAME='PriceAtTransaction')) "
        "AND DATA_TYPE='decimal' AND NUMERIC_PRECISION=20 AND NUMERIC_SCALE=4")
        .bind(schemaName).execute().fetchOne();
    if (moneyColumns[0].get<int>() != 3) {
        throw std::runtime_error("Money columns must use DECIMAL(20,4). See sql/schema.sql before migrating existing data.");
    }
    session_ = std::move(session);
}

int Database::createUser(const std::string& username, const std::string& password) {
    if (username.empty() || password.empty()) throw std::invalid_argument("Username and password are required.");
    auto result = session_->sql("INSERT INTO Users (Username, Password, Balance) VALUES (?, ?, 0)")
        .bind(username, password).execute();
    return static_cast<int>(result.getAutoIncrementValue());
}

int Database::loginUser(const std::string& username, const std::string& password) {
    auto row = session_->sql("SELECT UserID FROM Users WHERE Username=? AND Password=?")
        .bind(username, password).execute().fetchOne();
    if (row.isNull()) throw std::runtime_error("Invalid username or password.");
    return row[0].get<int>();
}

double Database::getBalance(int userID) {
    auto row = session_->sql("SELECT CAST(Balance AS DOUBLE) FROM Users WHERE UserID=?")
        .bind(userID).execute().fetchOne();
    if (row.isNull()) throw OrderRejected("User not found.");
    return row[0].get<double>();
}

void Database::lockAccount(int userID) {
    auto row = session_->sql("SELECT UserID FROM Users WHERE UserID=? FOR UPDATE")
        .bind(userID).execute().fetchOne();
    if (row.isNull()) throw OrderRejected("User not found.");
}

void Database::changeBalance(int userID, double amount, bool deposit) {
    const auto value = money(amount);
    Transaction transaction(*session_);
    lockAccount(userID);
    auto result = deposit
        ? session_->sql("UPDATE Users SET Balance=Balance+CAST(? AS DECIMAL(20,4)) WHERE UserID=?")
            .bind(value, userID).execute()
        : session_->sql("UPDATE Users SET Balance=Balance-CAST(? AS DECIMAL(20,4)) "
                        "WHERE UserID=? AND Balance>=CAST(? AS DECIMAL(20,4))")
            .bind(value, userID, value).execute();
    if (result.getAffectedItemsCount() != 1) throw OrderRejected("Insufficient funds.");
    transaction.commit();
}

void Database::depositMoney(int userID, double amount) { changeBalance(userID, amount, true); }
void Database::withdrawMoney(int userID, double amount) { changeBalance(userID, amount, false); }

void Database::buyStock(int userID, const std::string& symbol, int quantity) {
    executeOrder({0, userID, symbol, quantity, OrderSide::Buy});
}

void Database::sellStock(int userID, const std::string& symbol, int quantity) {
    executeOrder({0, userID, symbol, quantity, OrderSide::Sell});
}

void Database::executeOrder(const Order& order) {
    if (order.userID <= 0 || order.quantity <= 0 || !validStockSymbol(order.symbol) ||
        (order.side != OrderSide::Buy && order.side != OrderSide::Sell)) {
        throw OrderRejected("Invalid order.");
    }
    Transaction transaction(*session_);
    lockAccount(order.userID);
    auto stock = session_->sql(
        "SELECT CAST(StockPrice AS CHAR), CAST(StockPrice * ? AS CHAR), StockPrice > 0 "
        "FROM Stocks WHERE Symbol=? FOR SHARE")
        .bind(order.quantity, order.symbol).execute().fetchOne();
    if (stock.isNull()) throw OrderRejected("Unknown stock symbol.");
    if (!stock[2].get<int>()) throw OrderRejected("Stock price must be positive.");
    const std::string price = stock[0].get<std::string>();
    const std::string total = stock[1].get<std::string>();

    if (order.side == OrderSide::Sell) {
        // The account lock also protects holdings derived from its trade history.
        auto holdings = session_->sql(
            "SELECT CAST(COALESCE(SUM(CASE WHEN Type='Buy' THEN Quantity ELSE -Quantity END),0) "
            "AS SIGNED) FROM Transactions WHERE UserID=? AND Symbol=?")
            .bind(order.userID, order.symbol).execute().fetchOne();
        if (holdings[0].get<std::int64_t>() < order.quantity) {
            throw OrderRejected("Insufficient shares.");
        }
    }

    auto changed = order.side == OrderSide::Buy
        ? session_->sql("UPDATE Users SET Balance=Balance-CAST(? AS DECIMAL(20,4)) "
                        "WHERE UserID=? AND Balance>=CAST(? AS DECIMAL(20,4))")
            .bind(total, order.userID, total).execute()
        : session_->sql("UPDATE Users SET Balance=Balance+CAST(? AS DECIMAL(20,4)) WHERE UserID=?")
            .bind(total, order.userID).execute();
    if (changed.getAffectedItemsCount() != 1) throw OrderRejected("Insufficient funds.");

    session_->sql("INSERT INTO Transactions (UserID, Symbol, Quantity, PriceAtTransaction, Type) "
                  "VALUES (?, ?, ?, CAST(? AS DECIMAL(20,4)), ?)")
        .bind(order.userID, order.symbol, order.quantity, price,
              order.side == OrderSide::Buy ? "Buy" : "Sell").execute();
    transaction.commit();
}

void Database::viewPortfolio(int userID) {
    std::vector<mysqlx::Row> rows = session_->sql(
        "SELECT t.Symbol, CAST(SUM(CASE WHEN t.Type='Buy' THEN t.Quantity ELSE -t.Quantity END) AS SIGNED), "
        "CAST(s.StockPrice AS CHAR), "
        "CAST(SUM(CASE WHEN t.Type='Buy' THEN t.Quantity ELSE -t.Quantity END)*s.StockPrice AS CHAR) "
        "FROM Transactions t JOIN Stocks s ON s.Symbol=t.Symbol WHERE t.UserID=? "
        "GROUP BY t.Symbol, s.StockPrice HAVING SUM(CASE WHEN t.Type='Buy' THEN t.Quantity ELSE -t.Quantity END)<>0")
        .bind(userID).execute().fetchAll();
    if (rows.empty()) std::cout << "No holdings.\n";
    for (const auto& row : rows) {
        std::cout << row[0].get<std::string>() << " | Shares: " << row[1].get<std::int64_t>()
                  << " | Price: $" << row[2].get<std::string>()
                  << " | Value: $" << row[3].get<std::string>() << '\n';
    }
}

void Database::viewTransactions(int userID) {
    std::vector<mysqlx::Row> rows = session_->sql(
        "SELECT CAST(Date AS CHAR), Type, Quantity, Symbol, CAST(PriceAtTransaction AS CHAR) "
        "FROM Transactions WHERE UserID=? ORDER BY Date")
        .bind(userID).execute().fetchAll();
    if (rows.empty()) std::cout << "No transactions.\n";
    for (const auto& row : rows) {
        std::cout << row[0].get<std::string>() << " | " << row[1].get<std::string>()
                  << " | " << row[2].get<int>() << " " << row[3].get<std::string>()
                  << " | $" << row[4].get<std::string>() << '\n';
    }
}

std::vector<std::string> Database::returnStocks() {
    std::vector<mysqlx::Row> rows = session_->sql("SELECT Symbol FROM Stocks ORDER BY Symbol").execute().fetchAll();
    std::vector<std::string> symbols;
    for (const auto& row : rows) symbols.push_back(row[0].get<std::string>());
    return symbols;
}

void Database::updateStockPrices() {
    if (std::system("python3 update_stocks.py") != 0) {
        throw std::runtime_error("Stock price update failed.");
    }
}

std::string Database::getSentiment(const std::string& symbol, bool useTwitter) {
    if (!validStockSymbol(symbol)) throw std::invalid_argument("Invalid stock symbol.");
    const std::string command = "python3 sentiment.py " + symbol + (useTwitter ? " 1" : " 0");
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
    if (!pipe) throw std::runtime_error("Could not start sentiment script.");
    std::array<char, 256> buffer;
    std::string result;
    while (fgets(buffer.data(), buffer.size(), pipe.get())) result += buffer.data();
    const int status = pclose(pipe.release());
    if (status != 0 || result.empty()) throw std::runtime_error("Sentiment script failed.");
    return result;
}
