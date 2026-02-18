cmake_minimum_required(VERSION 3.16)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Boost REQUIRED COMPONENTS json)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System)

add_executable(orderbook
    BinanceAPIParser.cpp
    BinanceLiveMarketData.cpp
    BinanceOrderBookSync.cpp
    BinanceScalesSource.cpp
    BinanceSnapshotSource.cpp
    OrderBook.cpp
    Renderer.cpp
    SfmlRenderer.cpp
    main.cpp
)

target_compile_definitions(orderbook PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(orderbook PRIVATE -Wall -Wextra -Werror)
endif()

target_link_libraries(orderbook PRIVATE
    Boost::json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    SFML::Graphics
    SFML::Window
    SFML::System
)

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E echo "Removing all build artifacts from ${CMAKE_BINARY_DIR}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf
        ${CMAKE_BINARY_DIR}/CMakeFiles
        ${CMAKE_BINARY_DIR}/CMakeCache.txt
        ${CMAKE_BINARY_DIR}/cmake_install.cmake
        ${CMAKE_BINARY_DIR}/Makefile
        ${CMAKE_BINARY_DIR}/orderbook
        ${CMAKE_BINARY_DIR}/compile_commands.json
    COMMENT "Clean all CMake and build artifacts"
)
