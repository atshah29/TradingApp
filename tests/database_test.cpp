#include "mysql_fixture.h"

#include <atomic>
#include <cmath>
#include <limits>

namespace {
template<class F> void rejects(F action) {
    bool rejected = false;
    try { action(); } catch (const OrderRejected&) { rejected = true; }
    require(rejected, "Expected order rejection");
}

void overspending(MysqlFixture& fixture) {
    const int user = fixture.user(100);
    OrderProcessor processor(8, fixture.factory());
    for (int i = 0; i < 100; ++i) processor.submit(user, "TEST", 1, OrderSide::Buy);
    processor.waitIdle();
    int filled = 0, rejected = 0;
    for (const auto& result : processor.ordersFor(user)) {
        filled += result.status == OrderStatus::Filled;
        rejected += result.status == OrderStatus::Rejected;
    }
    require(filled == 10 && rejected == 90, "Concurrent buys overspent or failed unexpectedly");
    require(fixture.db.getBalance(user) == 0 && fixture.holdings(user) == 10, "Buy accounting mismatch");
    for (int i = 0; i < 100; ++i) processor.submit(user, "TEST", 1, OrderSide::Sell);
    processor.waitIdle();
    filled = rejected = 0;
    for (const auto& result : processor.ordersFor(user)) {
        filled += result.status == OrderStatus::Filled;
        rejected += result.status == OrderStatus::Rejected;
    }
    require(filled == 20 && rejected == 180, "Concurrent sells oversold or failed unexpectedly");
    require(fixture.db.getBalance(user) == 100 && fixture.holdings(user) == 0 && fixture.tradeCount(user) == 20,
            "Sell accounting mismatch");
}

void rollbackAndValidation(MysqlFixture& fixture) {
    const int user = fixture.user(100);
    rejects([&] { fixture.db.buyStock(user, "TEST", 0); });
    rejects([&] { fixture.db.sellStock(user, "TEST", -1); });
    rejects([&] { fixture.db.buyStock(user, "MISSING", 1); });
    rejects([&] { fixture.db.sellStock(user, "TEST", 1); });
    rejects([&] { fixture.db.buyStock(2147483647, "TEST", 1); });
    rejects([&] { fixture.db.withdrawMoney(user, 101); });
    rejects([&] { fixture.db.depositMoney(user, std::numeric_limits<double>::quiet_NaN()); });
    fixture.admin.sql("CREATE TRIGGER reject_trade BEFORE INSERT ON Transactions FOR EACH ROW "
                      "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected insert failure'").execute();
    bool failed = false;
    try { fixture.db.buyStock(user, "TEST", 1); } catch (const mysqlx::Error&) { failed = true; }
    fixture.admin.sql("DROP TRIGGER reject_trade").execute();
    require(failed && fixture.db.getBalance(user) == 100 && fixture.tradeCount(user) == 0,
            "Balance update did not roll back after trade insert failure");
    fixture.db.buyStock(user, "TEST", 1);
    require(fixture.db.getBalance(user) == 90, "Connection unusable after rollback");
    fixture.admin.sql("CREATE TRIGGER reject_sale BEFORE INSERT ON Transactions FOR EACH ROW "
                      "SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected sell failure'").execute();
    failed = false;
    try { fixture.db.sellStock(user, "TEST", 1); } catch (const mysqlx::Error&) { failed = true; }
    fixture.admin.sql("DROP TRIGGER reject_sale").execute();
    require(failed && fixture.db.getBalance(user) == 90 && fixture.holdings(user) == 1,
            "Sell credit did not roll back");
    fixture.db.sellStock(user, "TEST", 1);
    require(fixture.db.getBalance(user) == 100, "Sell failed after rollback");
}

void concurrentDeposits(MysqlFixture& fixture) {
    const int user = fixture.user(1000);
    OrderProcessor processor(4, fixture.factory());
    std::atomic<bool> failed{false};
    std::thread deposits([&] {
        try {
            Database db;
            db.connect(fixture.url, fixture.name);
            for (int i = 0; i < 100; ++i) db.depositMoney(user, 1);
        } catch (...) { failed = true; }
    });
    std::thread withdrawals([&] {
        try {
            Database db;
            db.connect(fixture.url, fixture.name);
            for (int i = 0; i < 100; ++i) db.withdrawMoney(user, 1);
        } catch (...) { failed = true; }
    });
    for (int i = 0; i < 50; ++i) processor.submit(user, "TEST", 1, OrderSide::Buy);
    deposits.join();
    withdrawals.join();
    processor.waitIdle();
    require(!failed && fixture.db.getBalance(user) == 500 && fixture.holdings(user) == 50,
            "Concurrent balance operations lost an update");
}

void decimalPrices(MysqlFixture& fixture) {
    fixture.admin.sql("UPDATE Stocks SET StockPrice=0.1000 WHERE Symbol='TEST'").execute();
    const int user = fixture.user(1);
    for (int i = 0; i < 10; ++i) fixture.db.buyStock(user, "TEST", 1);
    require(fixture.db.getBalance(user) == 0, "Decimal prices accumulated rounding errors");
    rejects([&] { fixture.db.buyStock(user, "TEST", 1); });
    fixture.db.sellStock(user, "TEST", 10);
    require(fixture.db.getBalance(user) == 1, "Decimal sale total is incorrect");
    fixture.admin.sql("UPDATE Stocks SET StockPrice=10 WHERE Symbol='TEST'").execute();
}

void overflowAndSchema(MysqlFixture& fixture) {
    const int user = fixture.user(100);
    fixture.admin.sql("UPDATE Stocks SET StockPrice=9999999999999999.9999 WHERE Symbol='TEST'").execute();
    bool failed = false;
    try { fixture.db.buyStock(user, "TEST", 2147483647); }
    catch (const std::exception&) { failed = true; }
    require(failed && fixture.db.getBalance(user) == 100 && fixture.tradeCount(user) == 0,
            "Overflow changed account state");
    fixture.admin.sql("UPDATE Stocks SET StockPrice=10 WHERE Symbol='TEST'").execute();
    fixture.admin.sql("ALTER TABLE Users MODIFY Balance DECIMAL(18,2) NOT NULL DEFAULT 0").execute();
    failed = false;
    try { Database db; db.connect(fixture.url, fixture.name); }
    catch (const std::runtime_error&) { failed = true; }
    fixture.admin.sql("ALTER TABLE Users MODIFY Balance DECIMAL(20,4) NOT NULL DEFAULT 0").execute();
    require(failed, "Unsupported money column type was accepted");
}
}

int main() {
    const char* url = std::getenv("MYSQLX_TEST_URL");
    if (!url) { std::cout << "Set MYSQLX_TEST_URL to run isolated MySQL tests.\n"; return 77; }
    try {
        MysqlFixture fixture(url);
        overspending(fixture);
        rollbackAndValidation(fixture);
        concurrentDeposits(fixture);
        decimalPrices(fixture);
        overflowAndSchema(fixture);
        std::cout << "MySQL concurrency and rollback tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
