# Order processing benchmark

Measured on 2026-10-24 using an Apple M4, macOS 15.7.3, AppleClang 15, a Release build, and MySQL 9.3.0 running locally. The test server used its default InnoDB settings and separate localhost ports. No external APIs or sentiment inference ran during measurement.

## Workload

Each invocation creates a temporary schema from [schema.sql](../sql/schema.sql), funds the accounts, and opens the workers' database connections. It executes one warmup buy per account, then submits a burst of 2,000 one-share buys distributed round-robin over either 100 accounts or one account. All trades use a stored price of $10.

One worker runs first, then four workers with fresh accounts. The reported values below are the median of each metric over three invocations per workload. [results.csv](results.csv) contains all 12 observations, including execution-time percentiles. The database server remains running between invocations; there is no cold-cache or randomized-order control.

## Results

| Accounts | Workers | Orders/sec | Median total latency (ms) | p95 total latency (ms) | Median execution time (ms) |
| --- | --- | --- | --- | --- | --- |
| 100 | 1 | 911.858 | 1131.897 | 2093.641 | 0.857 |
| 100 | 4 | 3645.962 | 283.375 | 522.436 | 0.900 |
| 1 | 1 | 1037.331 | 979.343 | 1832.730 | 0.816 |
| 1 | 4 | 1341.589 | 731.765 | 1417.630 | 2.562 |

Across the three runs with 100 accounts, one-worker throughput ranged from 805.506 to 994.791 orders/sec, and four-worker throughput ranged from 2964.920 to 3798.415 orders/sec. The ratio of median throughputs was approximately 4.0x. With one account, the ratio was approximately 1.3x, and execution time increased as workers waited for the same account lock.

Total latency measures submission through confirmed completion, including queue wait. Execution time starts after dequeue and includes database lock waits, queries, and commit. Throughput covers the submission burst and the time needed to finish every order. Connection setup, warmup, result inspection, and accounting verification are outside the timed region.

Every run verifies that all orders filled, that each account has the expected holdings and balance, and that its transaction count matches. A failed verification makes the benchmark exit unsuccessfully.

## Reproduce

Build the application in Release mode, then point the benchmark at a test MySQL server. The database user needs permission to create and drop temporary databases.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
export MYSQLX_TEST_URL='mysqlx://test_user:password@127.0.0.1:33060'
./build/order_benchmark 2000 100
./build/order_benchmark 2000 1
```

Run each command three times to reproduce the sampling method. Temporary schemas use the `trading_test_*` prefix and are removed on normal completion and handled exceptions. A killed process may leave its test schema behind.

## Interpretation

This benchmark measures simulated market orders against a local database, with sufficient funds and fixed prices. It is a burst-load test, not a steady-rate service test. The large total latencies reflect queued work. Hardware, storage, MySQL configuration, account contention, workload ordering, and concurrent activity affect the results.

The test does not measure exchange matching, network market feeds, sentiment inference, or API-cache performance. It does not establish a production latency guarantee. A resume claim should name the local benchmark and workload if it includes these figures.

A supported description is:

> Built a multithreaded C++/MySQL trading simulator with queued order processing, per-worker connections, transactional balance updates, and order-status tracking.
