cmake_minimum_required(VERSION 3.14)
project(HttpClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_EXAMPLES "Build example programs" OFF)

# Find libcurl
find_package(CURL REQUIRED)

# HttpClient library
add_library(HttpClient
    src/HttpClient.cpp
)

target_include_directories(HttpClient
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CURL_INCLUDE_DIRS}
)

target_link_libraries(HttpClient
    PRIVATE
        CURL::libcurl
)

add_executable(example EXCLUDE_FROM_ALL examples/example.cpp)
target_link_libraries(example PRIVATE HttpClient)

