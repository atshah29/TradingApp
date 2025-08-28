#include "database.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <thread>


void Database::connect(const std::string& url){
    if(session){
        std::cerr << "Session already exists. Please close it before creating a new one." << std::endl;
        return;
    }
    session = std::make_unique<mysqlx::Session>(url); //needed to use std::make_unique to initialize session
    if(session){
        std::cout << "SESSION FOUND" << std::endl;
    }
    schema = std::make_unique<mysqlx::Schema>(session->getSchema("trading", true)); //createSchema
    std::thread updateStocksThread (&Database::updateStockPrices, this); // Update stock prices on connection
    updateStocksThread.detach();
}

mysqlx::Schema &Database::getSchema() const{
    return *schema;
}


mysqlx::Table Database::getTable(const std::string & name){
    return schema->getTable(name);
}

Database::~Database(){
    if(session){
        session ->close();
    }
}

Database::Database() 
{
    // Constructor body is empty because the schema will be initialized when connect() is called
}



int Database::createUser(const std::string& username, const std::string& password) {
    // Get the Users table
    mysqlx::Table users = schema->getTable("Users");

    // Check if the user already exists
    mysqlx::RowResult check = users.select("Username")
                                      .where("Username = :username")
                                      .bind("username", username)
                                      .execute();
    if(check.count()>0){
        throw std::runtime_error("User already exists with the given username.");
    }



    // Insert new user with default balance of 0.0
    users.insert("Username", "Password", "Balance")
         .values(username, password, 0.0)
         .execute();

    // Retrieve the newly created user's ID
    mysqlx::RowResult result = users.select("UserID")
                                 .where("Username = :username AND Password = :password")
                                 .bind("username", username)
                                 .bind("password", password)
                                 .execute();

    mysqlx::Row row = result.fetchOne();

    if (!row.isNull()) {
        return row[0].get<int>(); // Return the UserID
    } else {
        throw std::runtime_error("Failed to retrieve newly created user.");
    }
}


int Database::loginUser (const std::string& username, const std::string& password){
    mysqlx::Table users = schema->getTable("Users");

    mysqlx::RowResult check = users.select("UserID")
                                .where("Username = :username AND Password = :password")
                                .bind("username", username)
                                .bind("password", password)
                                .execute();

    mysqlx::Row row = check.fetchOne();

    if(row.isNull()){
        throw std::runtime_error("Invalid username or password.");
    }
    else{
        return row.get(0);
    }

}


double Database::getBalance (int userID){
    mysqlx::Table users = schema->getTable("Users");

    mysqlx::RowResult result = users.select("Balance")
                                .where("UserID = :userID")
                                .bind("userID", userID)
                                .execute();



    mysqlx::Row row = result.fetchOne();

    if(row.isNull()){
        throw std::runtime_error("User not found.");
    }
    else{
        return row.get(0);
    }

}


void Database::depositMoney(int userID, double amount){
    if(amount <=0){
        throw std::runtime_error("Deposit amount must be positive.");
    }

    mysqlx::Table users = schema->getTable("Users");

    mysqlx::RowResult result = users.select("Balance")
                                .where("UserID = :userID")
                                .bind("userID", userID)
                                .execute();

    auto row = result.fetchOne();

    if(row.isNull()){
        throw std::runtime_error("User not found.");
    }
    else{
        double currentBalance = row.get(0);
        double newBalance  = currentBalance + amount; 
        users.update()
                .set("Balance", newBalance)
                .where("UserID = :userID")
                .bind("userID", userID)
                .execute();
    }
}

void Database::withdrawMoney (int userId, double amount){
    if(amount <=0){
        throw std::runtime_error("Withdrawal amount must be positive.");
    }

    mysqlx::Table users = schema->getTable("Users");

    mysqlx::RowResult result = users.select("Balance")
                                .where("UserID = :userID")
                                .bind("userID", userId)
                                .execute();
    
    auto row = result.fetchOne();

    if(row.isNull()){
        throw std::runtime_error("User not found.");
    }

    else{
        double currentBalance = row.get(0);
        if(currentBalance < amount){
            throw std::runtime_error("Insufficient funds for withdrawal.");
        }
        double newBalance = currentBalance - amount;

        users.update()
                .set("Balance", newBalance)
                .where("UserID = :userId")
                .bind("userId", userId)
                .execute();

    }
    

}


void Database::buyStock (int userID, const std::string& stockSymbol, int quantity){
    mysqlx::Table users = schema->getTable("Users");
    mysqlx::Table stocks = schema->getTable("Stocks");
    mysqlx::Table transactions = schema->getTable("Transactions");

    // Check if user exists
    mysqlx::RowResult userCheck = users.select("UserID")
                                        .where("UserID = :userID")
                                        .bind("userID", userID)
                                        .execute();
    
    mysqlx::Row userRow = userCheck.fetchOne();

    if(userRow.isNull()){
        throw std::runtime_error("User not found");
    }

    double userBalance = getBalance(userID);

    // Get stock price and check if stock exists

    mysqlx::RowResult stockPrice = stocks.select("StockPrice")
                                        .where("Symbol = :stockSymbol")
                                        .bind("stockSymbol", stockSymbol)
                                        .execute();
    
    mysqlx::Row stockRow = stockPrice.fetchOne();

    if(stockRow.isNull()){
        throw std::runtime_error("Stock not found with the given symbol.");
    }

    double stockPriceValue = (double) stockRow.get(0);


    // Check if user has enough balance

    if(stockPriceValue * quantity > userBalance){
        throw std::runtime_error("Insufficient funds to buy stock.");
    }


    //Deduct amount from user balance  

    withdrawMoney (userID, stockPriceValue * quantity);


    // Insert transaction record

    transactions.insert("UserID", "Symbol", "Quantity", "PriceAtTransaction", "Type")
                .values(userID, stockSymbol, quantity, stockPriceValue, "Buy")
                .execute();

}


void Database::sellStock (int userID, const std::string& stockSymbol, int quantity){
    if (quantity <= 0) {
        throw std::runtime_error("Quantity to sell must be positive.");
    }


    mysqlx::Table users = schema->getTable("Users");
    mysqlx::Table stocks = schema->getTable("Stocks");
    mysqlx::Table transactions = schema->getTable("Transactions");

    // Check if user exists
    mysqlx::RowResult userCheck = users.select("UserID")
                                        .where("UserID = :userID")
                                        .bind("userID", userID)
                                        .execute();
    
    mysqlx::Row userRow = userCheck.fetchOne();

    if(userRow.isNull()){
        throw std::runtime_error("User not found");
    }

    double userBalance = getBalance(userID);

    // Get stock price and check if stock exists

    mysqlx::RowResult stockPrice = stocks.select("StockPrice")
                                        .where("Symbol = :stockSymbol")
                                        .bind("stockSymbol", stockSymbol)
                                        .execute();
    
    mysqlx::Row stockRow = stockPrice.fetchOne();

    if(stockRow.isNull()){
        throw std::runtime_error("Stock not found with the given symbol.");
    }

    double stockPriceValue = (double) stockRow.get(0);

    // Check if user has enough stock to sell

    mysqlx::RowResult result = transactions.select("Type", "SUM(Quantity)")
        .where("UserID = :userID AND Symbol = :stockSymbol")
        .groupBy("Type")
        .bind("userID", userID)
        .bind("stockSymbol", stockSymbol)
        .execute();

    int totalQuantity = 0;
    std::vector<mysqlx::Row> resultRows = result.fetchAll();

    for(auto & row : resultRows){
        std::string type = (std::string)row.get(0);
        double quantityDouble = row.get(1).get<double>();
        int summedQuantity = static_cast<int>(quantityDouble);

        if(type == "Buy"){
            totalQuantity += summedQuantity; // Add bought stocks
        } else if(type == "Sell"){
            totalQuantity -= summedQuantity; // Subtract sold stocks
        }

    }



    if(totalQuantity < quantity){
        throw std::runtime_error("Insufficient stock to sell.");
    }

    // Add amount to user balance

    double totalSaleValue = stockPriceValue * quantity;

    depositMoney(userID, totalSaleValue);

    // Insert transaction record

    transactions.insert("UserID", "Symbol", "Quantity", "PriceAtTransaction", "Type")
                .values(userID, stockSymbol, quantity, stockPriceValue, "Sell")
                .execute();

}

void Database::viewPortfolio(int userID){
    mysqlx::Table transactions = schema->getTable("Transactions");
    mysqlx::Table stocks = schema->getTable("Stocks");

    //Check if user has transactions

    mysqlx::RowResult userCheck = transactions.select("UserID")
        .where("UserID = :userID")
        .bind("userID", userID)
        .execute();

    if (userCheck.fetchOne().isNull()) {
        std::cout << "No transactions found for user ID: " << userID << std::endl;
        return;
    }

    mysqlx::RowResult result = transactions.select("Symbol", "Type", "SUM(Quantity)")
        .where("UserID =:userID")
        .groupBy("Symbol", "Type")
        .bind("userID", userID)
        .execute();

    std::unordered_map<std::string, int> portfolio;
    std::vector <mysqlx::Row> resultRows = result.fetchAll();

    for( auto & row : resultRows){
        std::string symbol = (std::string) row.get(0);
        std::string type = (std::string) row.get(1);    
        double quantityDouble = row.get(2).get<double>();
        int quantity = static_cast<int>(quantityDouble);

        if(type == "Buy"){
            portfolio[symbol] += quantity; // Add bought stocks
        } else if(type == "Sell"){
            portfolio[symbol] -= quantity; // Subtract sold stocks
        }
    }

    for ( auto iter = portfolio.begin(); iter != portfolio.end(); iter++){
            // Get stock price and check if stock exists

        if(iter->second == 0){
            continue; 
        }

        mysqlx::RowResult stockPrice = stocks.select("StockPrice")
                                        .where("Symbol = :stockSymbol")
                                        .bind("stockSymbol", iter->first)
                                        .execute();
    
        mysqlx::Row stockRow = stockPrice.fetchOne();

        double stockPriceValue = stockRow.get(0).get<double>();

        std::cout << "Stock: " << iter->first
                << " | Quantity: " << iter->second
                << " | Price: $" << stockPriceValue
                << " | Total Value: $" << (iter->second * stockPriceValue)
                << "\n";
    }

}


void Database::viewTransactions (int userID){
    mysqlx::Table transactions = schema->getTable("Transactions");

    //Check if the user has transactions

    mysqlx::RowResult userCheck = transactions.select("Date", "Type", "Quantity", "Symbol", "PriceAtTransaction")
        .where("UserID = :userID")
        .orderBy("Date ASC")
        .bind("userID", userID)
        .execute();

    std::vector <mysqlx::Row> resultRows = userCheck.fetchAll();

    if(resultRows.empty()){
        std::cout << "No transactions found for user ID: " << userID << std::endl;
        return;
    }

    std::cout << "Transactions for user ID: " << userID << std::endl;

    for (auto& row : resultRows){
        std::string date = (std::string) row.get(0);
        std::string type = (std::string) row.get(1);
        int quantity = (int) row.get(2);
        std::string symbol = (std::string) row.get(3);
        double priceAtTransaction = (double) row.get(4);

        std::cout << "Date: " << date
                  << " | Type: " << type
                  << " | Quantity: " << quantity
                  << " | Symbol: " << symbol
                  << " | Price at Transaction: $" << priceAtTransaction
                  << "\n";
    }


}


void Database::updateStockPrices() {
    int result = std::system ("python3 /Users/aadeshshah/TradingApp/update_stocks.py"); // Assuming you have a Python script to update stock prices
    // if(result != 0) {
    //     std::cerr << "Failed to update stock prices. Please check the Python script or API is overused" << std::endl;
    // } else {
    //     std::cout << "Stock prices updated successfully." << std::endl;
    // }

    
}


std::string execCommand(const std::string& cmd) {
    std::array<char, 128> buffer;
    std::string result;

    // Open pipe to file
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }

    // read until end of process:
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::string Database::getSentiment(const std::string& stockSymbol, bool useTwitter) {
    std::string cmd = "python3 /Users/aadeshshah/TradingApp/sentiment.py " 
                    + stockSymbol + " " + (useTwitter ? "1" : "0");

    std::string output = execCommand(cmd);
    if (output.empty()) {
        throw std::runtime_error("Python script returned no output.");
    }
    return output; // return the captured sentiment string
}


std::vector<std::string> Database::returnStocks(){
    std::vector<std::string> names;
    mysqlx::Table stocks = schema->getTable("Stocks");

    mysqlx::RowResult stockNames = stocks.select("Symbol")
                                   .execute();

    std::vector <mysqlx::Row> resultRows = stockNames.fetchAll();

    for (auto& row : resultRows){
        names.emplace_back((std::string) row.get(0));
    }

    return names;

} 



