#pragma once

#include "database.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>

inline void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

class MysqlFixture {
public:
    explicit MysqlFixture(std::string connectionURL)
        : url(std::move(connectionURL)), admin(url),
          name("trading_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
               "_" + std::to_string(std::random_device{}())) {
        admin.sql("CREATE DATABASE `" + name + "`").execute();
        try {
            admin.sql("USE `" + name + "`").execute();
            std::ifstream schema(SCHEMA_FILE);
            if (!schema) throw std::runtime_error("Cannot open schema.sql");
            std::string statement;
            while (std::getline(schema, statement, ';')) {
                if (statement.find_first_not_of(" \t\r\n") != std::string::npos) admin.sql(statement).execute();
            }
            db.connect(url, name);
            admin.sql("INSERT INTO Stocks VALUES ('TEST', 'Test stock', 10.0000)").execute();
        } catch (...) {
            admin.sql("DROP DATABASE `" + name + "`").execute();
            throw;
        }
    }
    ~MysqlFixture() {
        try { admin.sql("DROP DATABASE `" + name + "`").execute(); }
        catch (const std::exception& error) { std::cerr << "Could not remove " << name << ": " << error.what() << '\n'; }
    }
    OrderProcessor::ExecutorFactory factory() const {
        return [url = url, name = name] {
            auto connection = std::make_shared<Database>();
            connection->connect(url, name);
            return [connection](const Order& order) { connection->executeOrder(order); };
        };
    }
    int user(double balance) {
        const int id = db.createUser("user" + std::to_string(++userCount_), "test-only");
        if (balance > 0) db.depositMoney(id, balance);
        return id;
    }
    std::int64_t holdings(int userID) {
        return admin.sql("SELECT CAST(COALESCE(SUM(CASE WHEN Type='Buy' THEN Quantity ELSE -Quantity END),0) AS SIGNED) "
                         "FROM Transactions WHERE UserID=? AND Symbol='TEST'")
            .bind(userID).execute().fetchOne()[0].get<std::int64_t>();
    }
    std::int64_t tradeCount(int userID) {
        return admin.sql("SELECT COUNT(*) FROM Transactions WHERE UserID=?")
            .bind(userID).execute().fetchOne()[0].get<std::int64_t>();
    }
    std::string url;
    mysqlx::Session admin;
    std::string name;
    Database db;
private:
    int userCount_ = 0;
};
