cmake_minimum_required(VERSION 3.10)
project(TradingApp VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add your source files
add_executable(TradingApp main.cpp database.cpp)

# Include directories for headers
target_include_directories(TradingApp PRIVATE /opt/homebrew/opt/mysql-connector-c++/include/mysqlx/)

# Link directory for .dylib files
target_link_directories(TradingApp PRIVATE /opt/homebrew/opt/mysql-connector-c++/lib)

# Link the MySQL C++ connector (X DevAPI)
target_link_libraries(TradingApp PRIVATE mysqlcppconnx)


# Set debugging properties to use external terminal
set_target_properties(TradingApp PROPERTIES
    VS_DEBUGGER_CONSOLE "true"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)

#