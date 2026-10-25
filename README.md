# TradingApp

A C++17 command-line trading simulator by Aadesh Shah, with MySQL persistence and Python-based financial sentiment analysis. Orders execute against stored stock prices; there is no exchange or brokerage connection.

## Features

- Account creation, login, and simulated cash deposits.
- Asynchronous buy/sell orders processed by a configurable worker pool.
- Transactional balance and trade updates with protection against concurrent overspending and overselling.
- Order IDs, status, failure reasons, and submission-to-completion timing.
- Portfolio and transaction history queries.
- News and optional Twitter/X sentiment using a separately supplied FinBERT model.
- Background price updates and a news-sentiment cache.

## How orders work

```text
CLI -> queue -> worker's MySQL connection -> transaction -> order status
```

Submitting an order puts it in a mutex-protected queue and wakes a worker through a condition variable. Idle workers sleep. A worker removes one order, releases the queue lock, and executes it using its own database connection.

Each trade starts a MySQL transaction and locks the user's account row with `FOR UPDATE`. The worker checks funds or holdings, updates the balance, and inserts the trade record before committing. Failures roll back the transaction. Deposits and withdrawals take the same account lock, so they cannot overwrite a concurrent trade's balance change. A shared lock holds the selected stock price stable until the trade finishes.

Different accounts can execute concurrently. Orders for one account serialize at its database row. The queue is FIFO, but with multiple workers, execution and completion order are not guaranteed, even for the same account. Wait for a buy to fill before submitting a dependent sell.

Money is stored and calculated in MySQL as `DECIMAL(20,4)`. Connections use `READ COMMITTED` isolation so holdings queries see earlier committed trades after acquiring the account lock, and strict SQL mode rejects numeric overflow.

Order states:

| Status | Meaning |
| --- | --- |
| Pending | Accepted into the queue |
| Processing | Taken by a worker |
| Filled | MySQL confirmed the commit |
| Rejected | A business check failed, such as insufficient funds or shares |
| Failed | An unexpected execution or database error occurred |

A connection failure during commit can leave the outcome uncertain. Check transaction history before resubmitting a failed order; the app does not automatically retry it. Order status is held in memory for the current process. Successful trade records remain in MySQL.

Shutdown stops accepting orders, drains the queue, and joins the workers. The market-data thread is also joined; exit can wait for its current Python subprocess to finish.

## Project layout

| File | Purpose |
| --- | --- |
| [main.cpp](main.cpp) | CLI, order status display, and background market data |
| [order_processor.h](order_processor.h), [order_processor.cpp](order_processor.cpp) | Queue, worker lifecycle, status, and timing |
| [database.h](database.h), [database.cpp](database.cpp) | Connections, transactional trading, portfolios, and Python subprocesses |
| [sql/schema.sql](sql/schema.sql) | InnoDB tables and decimal money columns |
| [sentiment.py](sentiment.py), [update_stocks.py](update_stocks.py) | Sentiment inference and stock-price updates |
| [tests/](tests/) | Queue tests and isolated MySQL integration tests |
| [benchmarks/](benchmarks/) | Repeatable MySQL order benchmark and measured results |

## Setup

### Dependencies

The application needs a C++17 compiler, CMake 3.13+, MySQL 8.0.17+ with X Protocol enabled, and MySQL Connector/C++ with X DevAPI (`mysqlcppconnx`). The included version-8 CMake preset requires CMake 3.28+.

On macOS with Homebrew:

```sh
xcode-select --install  # If Command Line Tools are not installed
brew install cmake mysql mysql-connector-c++ python@3.12
```

CMake searches the normal system locations and the Apple Silicon Homebrew connector directory. If necessary, pass `-DMYSQLX_INCLUDE_DIR=/path/to/include` and `-DMYSQLX_LIBRARY=/path/to/libmysqlcppconnx.dylib` when configuring.

### Database

For a new local database, start MySQL and run:

```sh
mysql -u root -p -e 'CREATE DATABASE trading;'
mysql -u root -p trading < sql/schema.sql
```

The schema creates `Users`, `Stocks`, and `Transactions`. Seed a stock before submitting orders, for example:

```sql
INSERT INTO trading.Stocks (Symbol, CompanyName, StockPrice)
VALUES ('TEST', 'Example stock', 10.0000);
```

Existing databases need compatible tables: InnoDB, primary keys on `Users.UserID` and `Stocks.Symbol`, and `DECIMAL(20,4)` for `Balance`, `StockPrice`, and `PriceAtTransaction`. The app checks the table engines and money types at connection time. The schema uses `CREATE TABLE IF NOT EXISTS`; it does not migrate existing tables. Back up and review existing data before changing its schema.

### Optional market data

Order processing works without Python, model files, or API credentials when launched with `--no-market-data`. The sentiment menu still requires them if selected.

To enable market data, run these commands from the repository root:

```sh
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
cp apis.env.example apis.env
```

Fill in the local `apis.env` with `newsAPI`, `bearerToken`, and `apiKey` for NewsAPI, Twitter/X, and Alpha Vantage. Both sentiment credentials are currently required even for news-only requests. Configure the stock updater's `MYSQL_HOST`, `MYSQL_PORT`, `MYSQL_USER`, `MYSQL_PASSWORD`, and `MYSQL_DATABASE` to point to the same database as the C++ app. The updater uses the classic MySQL port (normally 3306); the app uses X Protocol (normally 33060).

Place the fine-tuned model and tokenizer under `models/finetuned-finbert-2/`, including `model.safetensors`, `config.json`, and the tokenizer files. Loading is local-only. Model assets and training instructions are not included. A replacement model must use the `POSITIVE` and `NEGATIVE` labels expected by the aggregation code.

Run the app from the repository root with the virtual environment active so it can locate the scripts and their Python dependencies. The cache tracks the symbols present at startup. Prices are updated once in the background, and sentiment refreshes with a two-minute pause between passes.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/TradingApp --workers 4 --no-market-data
```

Omit `--no-market-data` to enable background API work. Worker count defaults to 4 and accepts values from 1 to 64.

Enter your MySQL X Protocol connection URL when prompted:

```text
mysqlx://user:password@localhost:33060
```

Create an account, log in, and deposit simulated funds. Buy/sell requests return an order ID immediately. Choose **8. Order status** to see their results. Portfolio and transaction views include committed trades only. Logging out does not cancel pending orders.

## Tests

Queue tests do not require MySQL. To build only those tests:

```sh
cmake -S . -B build/queue-tests -DBUILD_TRADING_APP=OFF
cmake --build build/queue-tests --parallel
ctest --test-dir build/queue-tests --output-on-failure
```

For the full build, set a connection URL to a **test server** whose user can create/drop databases and create triggers:

```sh
export MYSQLX_TEST_URL='mysqlx://test_user:password@127.0.0.1:33060'
ctest --test-dir build --output-on-failure
```

The MySQL tests create a uniquely named `trading_test_*` schema, then remove it. Without `MYSQLX_TEST_URL`, that test is marked skipped. Tests cover concurrent producers, status transitions, worker startup failure, shutdown, overspending, overselling, concurrent deposits/withdrawals, decimal accounting, overflow, and rollback after an injected trade-insert failure.

For a ThreadSanitizer run with a supported Clang installation:

```sh
cmake -S . -B build/tsan -DBUILD_TRADING_APP=OFF \
  -DCMAKE_CXX_FLAGS='-fsanitize=thread -g' \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build/tsan --parallel
ctest --test-dir build/tsan --output-on-failure
```

## Benchmark

With `MYSQLX_TEST_URL` set to a test server:

```sh
./build/order_benchmark 2000 100
./build/order_benchmark 2000 1
```

Each command compares one worker with four workers. Arguments are measured order count and account count. The benchmark warms each account, submits a burst of buys, waits for completion, and verifies every result, balance, holding, and trade count. CSV output includes throughput, median/p95 submission-to-completion latency, and median/p95 execution time. Connection setup, warmup, and final verification are excluded from timing.

See [benchmark results and limitations](benchmarks/README.md). Measurements describe this local simulator and workload, not exchange execution latency.

## Current limitations

- Passwords are stored in plaintext; this remains a local development prototype.
- Queue contents and statuses are in memory, unbounded, and lost on restart. There is no durable order recovery, cancellation, or idempotency key.
- Unexpected database errors are reported without automatic retry. A failed commit acknowledgement may require manual reconciliation.
- The stock updater uses Alpha Vantage's opening price field, not streaming prices. API quotas and the external company-name lookup can prevent updates.
- Market-data subprocesses are joined rather than forcibly interrupted. Python/model loading and external requests can delay exit.
- FinBERT accuracy and cache speedup claims require separate evaluation; the order benchmark does not measure them.

## Repository hygiene

Build output, caches, virtual environments, local credentials, and model assets are excluded by [.gitignore](.gitignore). Shared CMake files remain versioned; use `CMakeUserPresets.json` for personal presets. Removing generated files from the current branch does not erase them from older commits.
