cmake_minimum_required(VERSION 3.14)
project(HttpClient VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(HTTPCLIENT_BUILD_EXAMPLES "Build HttpClient example programs" OFF)
option(HTTPCLIENT_BUILD_TESTS "Build HttpClient tests" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

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
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)

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
set_target_properties(HttpClient PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
target_include_directories(HttpClient
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_definitions(HttpClient PUBLIC NOMINMAX)
if(BUILD_SHARED_LIBS)
    target_compile_definitions(HttpClient PRIVATE HTTPCLIENT_EXPORTS)
else()
    target_compile_definitions(HttpClient PUBLIC HTTPCLIENT_STATIC_DEFINE)
endif()
target_link_libraries(HttpClient
    PRIVATE
        $<BUILD_INTERFACE:MyCurl::curl>
        $<BUILD_INTERFACE:OpenSSL::Crypto>
        $<INSTALL_INTERFACE:CURL::libcurl>
        $<INSTALL_INTERFACE:OpenSSL::Crypto>
    INTERFACE
        $<BUILD_INTERFACE:MyCurl::curl>
        $<BUILD_INTERFACE:OpenSSL::Crypto>
        $<INSTALL_INTERFACE:CURL::libcurl>
        $<INSTALL_INTERFACE:OpenSSL::Crypto>
)

install(TARGETS HttpClient
    EXPORT HttpClientTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT HttpClientTargets
    FILE HttpClientTargets.cmake
    NAMESPACE HttpClient::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HttpClient
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/HttpClientConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/HttpClientConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HttpClient
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/HttpClientConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/HttpClientConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/HttpClientConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HttpClient
)

if(HTTPCLIENT_BUILD_EXAMPLES)
    add_executable(example examples/example.cpp)
    target_link_libraries(example PRIVATE HttpClient)
endif()

if(HTTPCLIENT_BUILD_TESTS)
    enable_testing()
    find_package(Threads REQUIRED)

    add_executable(throughput_tracker_test tests/ThroughputTrackerTest.cpp)
    target_include_directories(throughput_tracker_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(throughput_tracker_test PRIVATE Threads::Threads)
    add_test(NAME throughput_tracker_test COMMAND throughput_tracker_test)
endif()
