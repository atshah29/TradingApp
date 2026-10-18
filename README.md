# TradingApp

A C++ command-line trading simulation by Aadesh Shah, with MySQL persistence and Python-based financial sentiment analysis. Trades and balances are recorded in a local database; there is no brokerage integration.

## Features

- Create accounts, log in, and deposit simulated funds.
- Buy and sell stocks using prices stored in MySQL.
- View holdings, portfolio values, and transaction history.
- Analyze news headlines and optionally Twitter/X posts with a local fine-tuned FinBERT model.
- Cache news sentiment in a background thread, with a two-minute pause between refresh passes.
- Launch a stock-price update in the background when connecting to the database.

## Project layout

| File | Purpose |
| --- | --- |
| [main.cpp](main.cpp) | Interactive menus and background sentiment cache |
| [database.h](database.h), [database.cpp](database.cpp) | MySQL X DevAPI access, trades, portfolios, and Python subprocesses |
| [sentiment.py](sentiment.py) | NewsAPI/Twitter collection and local FinBERT inference |
| [update_stocks.py](update_stocks.py) | Populate or update stock prices using Alpha Vantage |
| [CMakeLists.txt](CMakeLists.txt), [CMakePresets.json](CMakePresets.json) | C++ build configuration |

## Prerequisites

The current build configuration targets macOS on Apple Silicon with Homebrew paths.

- A C++17 compiler and CMake 3.13 or newer (3.28 or newer for the included version-8 preset).
- MySQL Server with X Protocol enabled, normally on port `33060`.
- MySQL Connector/C++ with X DevAPI (`mysqlcppconnx`). The build expects it under `/opt/homebrew/opt/mysql-connector-c++`.
- Python 3.12 is recommended; Python packages are listed in [requirements.txt](requirements.txt).
- NewsAPI, Twitter/X bearer-token, and Alpha Vantage credentials.
- The fine-tuned model and tokenizer assets described below.

For macOS development tools and dependencies:

```sh
xcode-select --install  # If Apple Command Line Tools are not installed
brew install cmake mysql mysql-connector-c++ python@3.12
```

## Setup

### 1. Python dependencies and API credentials

From the repository root:

```sh
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
cp apis.env.example apis.env
```

Fill in the values in your local `apis.env`. Both Python scripts load this file relative to their own location. The sentiment script currently requires both `newsAPI` and `bearerToken`, even for news-only requests; the stock updater requires `apiKey` for Alpha Vantage. Keep the virtual environment active when launching the C++ app, since it invokes `python3`.

### 2. Local sentiment model

Place the fine-tuned Hugging Face model and tokenizer under `models/finetuned-finbert-2/`. The directory must contain the model weights (for example, `model.safetensors`), `config.json`, and tokenizer files such as `tokenizer.json`, `tokenizer_config.json`, `special_tokens_map.json`, and `vocab.txt`.

The loader uses `local_files_only=True`: it does not download a model automatically. Model assets and training instructions are not distributed with this repository. The sentiment aggregation expects `POSITIVE` and `NEGATIVE` labels, so a replacement model must use compatible label mappings.

### 3. MySQL database

Start MySQL and provision the schema before running the app. The repository does not yet include a schema migration or initialization script. The code expects these tables and columns:

| Table | Required columns |
| --- | --- |
| `Users` | `UserID`, `Username`, `Password`, `Balance` |
| `Stocks` | `Symbol`, `CompanyName`, `StockPrice` |
| `Transactions` | `UserID`, `Symbol`, `Quantity`, `PriceAtTransaction`, `Type`, `Date` |

Use an auto-generated `UserID`, a unique stock `Symbol` for the updater's upsert, and a default timestamp for transaction `Date`. Transaction types are `Buy` and `Sell`.

The C++ code opens schema `trading` and tables `Users`, `Stocks`, and `Transactions`. The Python updater currently connects to `Trading` and writes to `stocks`, using `root` with an empty password on `127.0.0.1`. Adjust its connection settings for your local database and align schema/table capitalization on case-sensitive MySQL installations. Its connection is separate from the URL entered in the C++ app.

### 4. Machine-specific paths

Before building on another machine, update the two Python script paths in [database.cpp](database.cpp), which currently point to `/Users/aadeshshah/TradingApp/`. Adjust the MySQL Connector include/library directories in [CMakeLists.txt](CMakeLists.txt) if your installation differs from the Homebrew layout above.

## Build and run

From the repository root, with the Python virtual environment active:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
./build/TradingApp
```

When prompted, enter your MySQL X Protocol connection URL:

```text
mysqlx://user:password@localhost:33060
```

Create an account or log in to use the trading menu. Seed the stock table before startup so the background sentiment worker has symbols to track. The price updater can be run separately after configuring its database connection:

```sh
python update_stocks.py
python sentiment.py AAPL      # News sentiment
python sentiment.py AAPL 1    # News plus Twitter/X sentiment
```

## Current limitations

- This is a development prototype. User passwords are stored in plaintext, and balance/trade updates are not wrapped in a single database transaction.
- Prices are snapshots: the updater uses Alpha Vantage's opening price field, not a streaming market feed. API quotas and the external company-name lookup can prevent updates.
- Background workers are detached and shutdown coordination is unfinished.
- A fresh clone still requires database provisioning, separately supplied model assets, and local configuration. There is no automated test suite yet.

## Repository hygiene

Build directories, compiled output, Python caches and virtual environments, macOS metadata, local credentials, and model assets are excluded by [.gitignore](.gitignore). Keep generated files local and rebuild them from source. Shared CMake configuration stays versioned; use `CMakeUserPresets.json` for personal presets.

Removing generated files from the current branch does not erase them from older commits.
