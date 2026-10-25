#include "tests/mysql_fixture.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>

namespace {
double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    return values.at(static_cast<std::size_t>(std::ceil(values.size() * fraction)) - 1);
}

void benchmark(MysqlFixture& fixture, int workers, int count, int accounts) {
    std::vector<int> users;
    for (int i = 0; i < accounts; ++i) users.push_back(fixture.user((count + 1) * 10.0));
    OrderProcessor processor(workers, fixture.factory());
    for (int user : users) processor.submit(user, "TEST", 1, OrderSide::Buy);
    processor.waitIdle();

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < count; ++i) processor.submit(users[i % accounts], "TEST", 1, OrderSide::Buy);
    processor.waitIdle();
    const auto stop = std::chrono::steady_clock::now();

    std::vector<double> latency, service;
    for (int i = 0; i < accounts; ++i) {
        const int expected = count / accounts + (i < count % accounts ? 1 : 0) + 1;
        require(fixture.holdings(users[i]) == expected && fixture.tradeCount(users[i]) == expected,
                "Benchmark holdings/trade count mismatch");
        require(fixture.db.getBalance(users[i]) == (count + 1 - expected) * 10.0,
                "Benchmark balance mismatch");
        for (const auto& result : processor.ordersFor(users[i])) {
            require(result.status == OrderStatus::Filled, "Benchmark order did not fill");
            if (result.order.id <= static_cast<std::uint64_t>(accounts)) continue;
            latency.push_back(std::chrono::duration<double, std::milli>(result.finished - result.submitted).count());
            service.push_back(std::chrono::duration<double, std::milli>(result.finished - result.started).count());
        }
    }
    require(latency.size() == static_cast<std::size_t>(count), "Missing benchmark observations");
    const double seconds = std::chrono::duration<double>(stop - start).count();
    std::cout << workers << ',' << count << ',' << accounts << ',' << std::fixed << std::setprecision(3)
              << count / seconds << ',' << percentile(latency, .5) << ',' << percentile(latency, .95)
              << ',' << percentile(service, .5) << ',' << percentile(service, .95) << '\n';
}
}

int main(int argc, char** argv) {
    const char* url = std::getenv("MYSQLX_TEST_URL");
    if (!url) { std::cerr << "Set MYSQLX_TEST_URL to a server that allows temporary test schemas.\n"; return 1; }
    try {
        const int count = argc > 1 ? std::stoi(argv[1]) : 2000;
        const int accounts = argc > 2 ? std::stoi(argv[2]) : 100;
        require(count > 0 && count <= 1000000 && accounts > 0 && accounts <= 1000, "Invalid benchmark size");
        MysqlFixture fixture(url);
        std::cout << "workers,orders,accounts,orders_per_second,p50_total_ms,p95_total_ms,p50_execution_ms,p95_execution_ms\n";
        benchmark(fixture, 1, count, accounts);
        benchmark(fixture, 4, count, accounts);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
