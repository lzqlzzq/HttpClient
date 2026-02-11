cmake_minimum_required(VERSION 3.14)
project(HttpClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_EXAMPLES "Build example programs" OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find libcurl
find_package(CURL 8.10.0 QUIET)
find_package(OpenSSL REQUIRED)

add_library(mycurl INTERFACE)

if (CURL_FOUND)
    message(STATUS "Using system libcurl: ${CURL_VERSION_STRING}")
    target_link_libraries(mycurl INTERFACE CURL::libcurl)
else()
    message(STATUS "System libcurl not found or too old; building vendored curl")

    include(FetchContent)

    # Build configuration
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)

    # Protocol configuration - HTTP(S) only
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)

    # Fetch curl from GitHub
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_11_1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(curl)

    target_link_libraries(mycurl INTERFACE libcurl_static)
    target_compile_definitions(mycurl INTERFACE CURL_STATICLIB)
endif()

# Alias for curl
add_library(MyCurl::curl ALIAS mycurl)

# HttpClient library
add_library(HttpClient
    src/HttpClient.cpp
    src/HashHelper.cpp
)
target_include_directories(HttpClient
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(HttpClient PRIVATE MyCurl::curl OpenSSL::Crypto)

add_executable(example EXCLUDE_FROM_ALL examples/example.cpp)
target_link_libraries(example PRIVATE HttpClient)
