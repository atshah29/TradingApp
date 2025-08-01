#include <xdevapi.h>
#include <string_view>
#include <iostream>
#include <string>
#include <memory>  // for std::unique_ptr


#ifndef DATABASE_H
#define DATABASE_H


class Database {

    private: 
        std::unique_ptr<mysqlx::Session> session;
        std::unique_ptr<mysqlx::Schema> schema;

    public:

    void connect(const std::string& url); 
    
    mysqlx::Schema &getSchema() const;

    mysqlx::Table getTable(const std::string & name);

    ~Database();

    Database();

    int createUser(const std::string& username, const std::string& password);

    int loginUser (const std::string& username, const std::string& password);

    double getBalance (int userID);

    void depositMoney(int userID, double amount);

    void withdrawMoney(int userID, double amount);

    void buyStock (int userID, const std::string& stockSymbol, int quantity);

    void sellStock (int userID, const std::string& stockSymbol, int quantity);

    void viewPortfolio(int userID);

    void viewTransactions(int userID);

    void updateStockPrices();

    void getSentiment(const std::string& stockSymbol, bool useTwitter);
}

;
#endif // DATABASE_H