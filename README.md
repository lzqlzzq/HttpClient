# HttpClient

A modern C++17 HTTP client library built on top of libcurl, featuring async/concurrent request support with connection pooling.

## Features

- **Blocking Requests**: Simple synchronous HTTP requests via `HttpTransfer`
- **Async Connection Pool**: Singleton `HttpClient` with thread-safe request queue
- **HTTP/2 Support**: Multiplexing via curl multi interface
- **Request Cancellation**: Cancel in-flight requests via `TransferState`
- **Connection Reuse**: Efficient TCP keep-alive and connection pooling
- **HTTP Methods**: GET, POST, HEAD, PATCH, PUT, DELETE, and custom methods

## Requirements

- C++17 compiler
- CMake 3.14+
- libcurl

## Integration

### Option 1: CMake FetchContent (Recommended)

Add to your `CMakeLists.txt`:

```cmake
include(FetchContent)

FetchContent_Declare(
    HttpClient
    GIT_REPOSITORY https://github.com/your-repo/HttpClient.git
    GIT_TAG main
)
FetchContent_MakeAvailable(HttpClient)

target_link_libraries(your_target PRIVATE HttpClient)
```

### Option 2: Add as Subdirectory

```bash
# Clone into your project
git clone https://github.com/your-repo/HttpClient.git 3rdparty/HttpClient
```

Add to your `CMakeLists.txt`:

```cmake
add_subdirectory(3rdparty/HttpClient)
target_link_libraries(your_target PRIVATE HttpClient)
```

## Build

```bash
# Build library only
cmake -B build
cmake --build build

# Build with examples
cmake -B build -DBUILD_EXAMPLES=ON
cmake --build build

# Run example
./build/example
```

## Usage

### Blocking Request (HttpTransfer)

For simple one-off requests, use `HttpTransfer` directly. Remember to call `curl_global_init()` and `curl_global_cleanup()`:

```cpp
#include "HttpClient.hpp"

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Create request (url, method, timeout_ms, headers, body)
    http_client::HttpRequest request(
        "https://httpbin.org/get",
        "GET",
        5000  // 5 second timeout
    );
    
    // Perform blocking request
    http_client::HttpTransfer transfer(request);
    transfer.perform_blocking();
    
    // Get response
    const auto& response = transfer.getResponse();
    std::cout << "Status: " << response.status << std::endl;
    std::cout << "Body: " << response.body << std::endl;
    
    curl_global_cleanup();
    return 0;
}
```

### POST with JSON Body

```cpp
std::string jsonBody = R"({"name":"test","value":"123"})";

http_client::HttpRequest request(
    "https://httpbin.org/post",
    "POST",
    5000,  // timeout_ms
    {"Content-Type: application/json"},  // headers
    jsonBody  // body
);

http_client::HttpTransfer transfer(request);
transfer.perform_blocking();
```

### Async Request (HttpClient Singleton)

The `HttpClient` singleton manages its own curl initialization and connection pool:

```cpp
#include "HttpClient.hpp"

int main() {
    auto& client = http_client::HttpClient::getInstance();
    
    // Synchronous request via pool (blocks until complete)
    auto response = client.request(
        "https://httpbin.org/get",
        "GET",
        10000  // timeout_ms
    );
    
    std::cout << "Status: " << response.status << std::endl;
    std::cout << "Body: " << response.body << std::endl;
    
    return 0;
}
```

### Fire-and-Forget with Cancellation

```cpp
auto& client = http_client::HttpClient::getInstance();

// Send async request
auto state = client.send_request(
    "https://httpbin.org/delay/10",
    "GET",
    0  // no timeout
);

// Option 1: Wait for result
state->future.wait();
auto response = state->future.get();

// Option 2: Cancel the request
state->cancel();
```

## API Reference

### HttpRequest

Constructor:
```cpp
HttpRequest(
    std::string url,
    std::string methodName,
    long timeout_ms = 0,
    std::vector<std::string> headers = {},
    std::string body = {}
);
```

| Field | Type | Description |
|-------|------|-------------|
| `url` | `std::string` | Request URL |
| `methodName` | `std::string` | HTTP method name |
| `method` | `Method` | Parsed method enum |
| `timeout_ms` | `long` | Timeout in milliseconds (0 = no timeout) |
| `headers` | `std::vector<std::string>` | HTTP headers (e.g. `"Content-Type: application/json"`) |
| `body` | `std::string` | Request body |

### HttpResponse

| Field | Type | Description |
|-------|------|-------------|
| `status` | `long` | HTTP status code |
| `headers` | `std::vector<std::string>` | Response headers |
| `body` | `std::string` | Response body |
| `error` | `std::string` | Error message (if any) |
| `elapsed` | `double` | Request duration in seconds |

### HttpClient

| Method | Description |
|--------|-------------|
| `getInstance()` | Get singleton instance |
| `request(url, method, timeout_ms, headers, body)` | Synchronous request (blocks until complete) |
| `send_request(url, method, timeout_ms, headers, body)` | Async request, returns `shared_ptr<TransferState>` |
| `stop()` | Stop client and cancel all pending requests |

### TransferState

| Field/Method | Description |
|--------------|-------------|
| `future` | `std::shared_future<HttpResponse>` to get result |
| `cancel()` | Cancel the in-flight request |
| `get_state()` | Get current state: `Ongoing`, `Completed`, or `Cancel` |

## Configuration

Default settings in the library:

```cpp
#define MAX_CONNECTION 8L    // Max concurrent connections
#define POLL_MS 100L         // Multi poll timeout
```
