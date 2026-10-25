cmake_minimum_required(VERSION 3.13)
project(TradingApp VERSION 0.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)
include(CTest)

add_library(order_processor order_processor.cpp)
target_include_directories(order_processor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(order_processor PUBLIC Threads::Threads)

option(BUILD_TRADING_APP "Build the MySQL application and integration tools" ON)
if(BUILD_TRADING_APP)
    find_path(MYSQLX_INCLUDE_DIR mysqlx/xdevapi.h
        HINTS /opt/homebrew/opt/mysql-connector-c++/include)
    find_library(MYSQLX_LIBRARY NAMES mysqlcppconnx
        HINTS /opt/homebrew/opt/mysql-connector-c++/lib)
    if(NOT MYSQLX_INCLUDE_DIR OR NOT MYSQLX_LIBRARY)
        message(FATAL_ERROR "Install MySQL Connector/C++ or set MYSQLX_INCLUDE_DIR and MYSQLX_LIBRARY.")
    endif()
    add_library(trading_database database.cpp)
    target_include_directories(trading_database PUBLIC ${MYSQLX_INCLUDE_DIR})
    target_link_libraries(trading_database PUBLIC order_processor ${MYSQLX_LIBRARY})

    add_executable(TradingApp main.cpp)
    target_link_libraries(TradingApp PRIVATE trading_database)
    set_target_properties(TradingApp PROPERTIES
        VS_DEBUGGER_CONSOLE true
        VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(order_benchmark benchmarks/order_benchmark.cpp)
    target_link_libraries(order_benchmark PRIVATE trading_database)
    target_compile_definitions(order_benchmark PRIVATE SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql")
endif()

if(BUILD_TESTING)
    add_executable(order_processor_tests tests/order_processor_test.cpp)
    target_link_libraries(order_processor_tests PRIVATE order_processor)
    add_test(NAME order_processor COMMAND order_processor_tests)
    set_tests_properties(order_processor PROPERTIES TIMEOUT 30)
    if(BUILD_TRADING_APP)
        add_executable(database_tests tests/database_test.cpp)
        target_link_libraries(database_tests PRIVATE trading_database)
        target_compile_definitions(database_tests PRIVATE SCHEMA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/sql/schema.sql")
        add_test(NAME database COMMAND database_tests)
        set_tests_properties(database PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
    endif()
endif()
