#include <iostream>
#include "database.h"



int main(int, char**){
    // Initialize the Python interpreter
    std::cout << "Hello, from TradingApp!\n";
    Database db;
    std::string url;
    std::cout << "Enter MySQL connection URL (e.g., mysqlx://user:password@localhost:33060): ";
    std::getline(std::cin, url);
    db.connect(url);

    while (true) {
        std::cout << "\n--- TradingApp Menu ---\n";
        std::cout << "1. Login as Existing User\n";
        std::cout << "2. Create New User\n";
        std::cout << "3. Exit\n";
        std::cout << "Enter your choice: ";
        int choice;
        std::cin >> choice;

        if (choice == 1) {
            std::cout << "Enter Username: ";
            std::string username;
            std::cin >> username;
            std::cout << "Enter Password: ";
            std::string password;
            std::cin >> password;
            int userID = db.loginUser(username, password);

            while(true){
                std::cout << "\n--- "<<username<<" Menu ---\n";
                std::cout << "1. Deposit Money\n";
                std::cout << "2. Buy Stock\n";
                std::cout << "3. Sell Stock\n";
                std::cout << "4. View Portfolio\n";
                std::cout << "5. View Transactions\n";
                std::cout << "6. Return Stock Sentiment\n";
                std::cout << "7. Logout\n";
                std::cout << "Enter your choice: ";
                int userChoice;
                std::cin >> userChoice;

                if (userChoice == 1) {
                    std::cout << "Enter amount to deposit: ";
                    double amount;
                    std::cin >> amount;
                    
                    db.depositMoney(userID, amount);
                } else if (userChoice == 2) {
                    std::cout << "Enter stock symbol: ";
                    std::string stockSymbol;
                    std::cin >> stockSymbol;
                    std::cout << "Enter quantity: ";
                    int quantity;
                    std::cin >> quantity;
                    
                    db.buyStock(userID, stockSymbol, quantity);
                } else if (userChoice == 3) {
                    std::cout << "Enter stock symbol: ";
                    std::string stockSymbol;
                    std::cin >> stockSymbol;
                    std::cout << "Enter quantity: ";
                    int quantity;
                    std::cin >> quantity;
                    
                    db.sellStock(userID, stockSymbol, quantity);
                } else if (userChoice == 4) {
                    db.viewPortfolio(userID);
                } else if (userChoice == 5) {
                    db.viewTransactions(userID);
                } else if (userChoice == 6) {
                    std::cout << "Enter stock symbol for sentiment analysis: ";
                    std::string stockSymbol;
                    std::cin >> stockSymbol;
                    std::cout << "Use Twitter for sentiment analysis? (1 for Yes, 0 for No): ";
                    int useTwitter;
                    std::cin >> useTwitter;
                    bool useTwitterBool = useTwitter; 
                    try {
                        db.getSentiment(stockSymbol, useTwitterBool);
                    } catch (const std::exception& e) {
                        std::cout << "Error retrieving sentiment: " << e.what() << "\n";
                }
                } else if (userChoice == 7) {
                    std::cout << "Logging out...\n";
                    break;
                } else {
                    std::cout << "Invalid choice. Try again.\n";
                }
            }
        } else if (choice == 2) {
            std::cout << "Enter username: ";
            std::string username;
            std::cin >> username;
            std::cout << "Enter password: ";
            std::string password;
            std::cin >> password;
            

            int newUserID = db.createUser(username, password);
            std::cout << "User created successfully! Your User ID is: " << newUserID << "\n";
        } else if (choice == 3) {
            std::cout << "Exiting application. Goodbye!\n";
            break;
        } else {
            std::cout << "Invalid choice. Please try again.\n";
        }
    }


    return 0;
}