cmake_minimum_required(VERSION 3.14)
project(HttpClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_EXAMPLES "Build example programs" OFF)

# Find libcurl
find_package(CURL 7.80.0 QUIET)

add_library(mycurl INTERFACE)

if (CURL_FOUND)
  message(STATUS "Using system libcurl: ${CURL_VERSION_STRING}")
  target_link_libraries(mycurl INTERFACE CURL::libcurl)
else()
    message(STATUS "System libcurl not found or too old; building vendored curl")

    # Minimal http client
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)

    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_PKGCONFIG OFF CACHE BOOL "" FORCE)

    set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)

    # Just use system openssl
    set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
    set(CMAKE_USE_SECTRANSP ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CMAKE_USE_OPENSSL OFF CACHE BOOL "" FORCE)

    add_subdirectory(3rdparty/curl EXCLUDE_FROM_ALL)

    target_link_libraries(mycurl INTERFACE libcurl)

    # Windows likes static lib
    target_compile_definitions(mycurl INTERFACE CURL_STATICLIB)
endif()

# Alias for curl
add_library(MyCurl::curl ALIAS mycurl)

# HttpClient library
add_library(HttpClient
    src/HttpClient.cpp
)
target_include_directories(HttpClient
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        MyCurl::curl
)
target_link_libraries(HttpClient PRIVATE MyCurl::curl)

add_executable(example EXCLUDE_FROM_ALL examples/example.cpp)
target_link_libraries(example PRIVATE HttpClient)

