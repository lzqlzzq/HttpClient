# HttpClient

A modern C++17 HTTP client library built on top of libcurl, featuring async/concurrent request support with connection pooling and flexible retry policies.

## Features

- **Blocking Requests**: Simple synchronous HTTP requests via `HttpTransfer`
- **Async Connection Pool**: Singleton `HttpClient` with thread-safe request queue
- **HTTP/2 Support**: Multiplexing via curl multi interface
- **Request Control**: Cancel, pause, and resume in-flight requests via `TransferState`
- **Retry Policies**: Configurable retry with exponential/linear/fixed backoff strategies
- **Connection Reuse**: Efficient TCP keep-alive and connection pooling
- **Speed Tracking**: Real-time upload/download speed monitoring
- **Hash Utilities**: Built-in support for MD5, SHA-1/2/3, BLAKE2, SM3, etc.
- **HTTP Methods**: GET, POST, HEAD, PATCH, PUT, DELETE, and custom methods

## Requirements

- C++17 compiler
- CMake 3.14+
- libcurl 8.10.0+ (or auto-fetched if not available)
- OpenSSL

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

For simple one-off requests, use `HttpTransfer` directly:

```cpp
#include "HttpClient.hpp"

int main() {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    http_client::HttpRequest request;
    request.url = "https://httpbin.org/get";
    request.methodName = "GET";

    http_client::HttpTransfer transfer(request);
    transfer.perform_blocking();

    const auto& response = transfer.getResponse();
    std::cout << "Status: " << response.status << std::endl;
    std::cout << "Body: " << response.body << std::endl;

    curl_global_cleanup();
    return 0;
}
```

### POST with JSON Body

```cpp
http_client::HttpRequest request;
request.url = "https://httpbin.org/post";
request.methodName = "POST";
request.headers = {"Content-Type: application/json"};
request.body = R"({"name":"test","value":"123"})";

http_client::HttpTransfer transfer(request);
transfer.perform_blocking();
```

### Async Request (HttpClient Singleton)

The `HttpClient` singleton manages its own curl initialization and connection pool:

```cpp
#include "HttpClient.hpp"

int main() {
    auto& client = http_client::HttpClient::getInstance();

    http_client::HttpRequest request;
    request.url = "https://httpbin.org/get";
    request.methodName = "GET";

    // Synchronous request via pool (blocks until complete)
    auto response = client.request(request);

    std::cout << "Status: " << response.status << std::endl;
    std::cout << "Elapsed: " << response.transferInfo.total << "s" << std::endl;

    return 0;
}
```

### Async with Cancellation

```cpp
auto& client = http_client::HttpClient::getInstance();

http_client::HttpRequest request;
request.url = "https://httpbin.org/delay/10";
request.methodName = "GET";

auto state = client.send_request(request);

// Option 1: Wait for result
state->future.wait();
auto response = state->future.get();

// Option 2: Cancel the request
state->cancel();
```

### Pause and Resume

```cpp
auto state = client.send_request(request);

// Pause transfer
state->pause();

// Resume later
state->resume();

// Wait for completion
auto response = state->future.get();
```

### Retry with Backoff

```cpp
#include "HttpClient.hpp"
#include "RetryStrategies.hpp"

auto& client = http_client::HttpClient::getInstance();

http_client::HttpRequest request;
request.url = "https://httpbin.org/status/503";
request.methodName = "GET";

// Configure retry policy
http_client::RetryPolicy retryPolicy;
retryPolicy.maxRetries = 3;
retryPolicy.totalTimeout = 30.0f;  // 30 seconds total

// Retry on HTTP 5xx errors
retryPolicy.shouldRetry = http_client::retry::httpStatusCondition({500, 502, 503, 504});

// Exponential backoff: 1s, 2s, 4s...
retryPolicy.getNextRetryTime = http_client::retry::exponentialBackoff(
    1.0,   // baseDelay
    10.0,  // maxDelay
    2.0,   // multiplier
    0.2    // jitterFactor
);

auto response = client.request(request, http_client::RequestPolicy(), retryPolicy);
```

### Combining Retry Conditions

```cpp
using namespace http_client::retry;

// Retry on CURL errors OR specific HTTP status codes
retryPolicy.shouldRetry = anyOf(
    defaultCondition(),                    // CURL errors
    httpStatusCondition({429, 500, 503})   // Rate limit + server errors
);

// Use the longer delay between exponential and fixed
retryPolicy.getNextRetryTime = maxOf(
    exponentialBackoff(0.1, 30.0),
    fixedDelay(1.0)
);
```

### Request Policy (Timeouts & Speed Limits)

```cpp
http_client::RequestPolicy policy;
policy.timeout = 30.0f;        // Total timeout in seconds
policy.connTimeout = 10.0f;    // Connection timeout
policy.lowSpeedLimit = 1024;   // Minimum bytes/sec
policy.lowSpeedTime = 30;      // Abort if below limit for this many seconds
policy.sendSpeedLimit = 0;     // Upload speed limit (0 = unlimited)
policy.recvSpeedLimit = 0;     // Download speed limit (0 = unlimited)

auto response = client.request(request, policy);
```

### Hash Utilities

```cpp
#include "HashHelper.hpp"

// One-shot hashing
std::string hash = http_client::Hash::sha256("Hello, World!");

// Streaming hash
http_client::Hash hasher = http_client::Hash::sha256();
hasher.update("Hello, ");
hasher.update("World!");
std::string hash = hasher.final();

// From stream
std::ifstream file("data.bin", std::ios::binary);
http_client::Hash hasher = http_client::Hash::md5();
hasher << file;
std::string hash = hasher.final();
```

Supported algorithms: `md5`, `sha1`, `sha224`, `sha256`, `sha384`, `sha512`, `sha512_224`, `sha512_256`, `sha3_224`, `sha3_256`, `sha3_384`, `sha3_512`, `blake2s256`, `blake2b512`, `ripemd160`, `sm3`

## API Reference

### HttpRequest

| Field | Type | Description |
|-------|------|-------------|
| `url` | `std::string` | Request URL |
| `methodName` | `std::string` | HTTP method name (GET, POST, etc.) |
| `headers` | `std::vector<std::string>` | HTTP headers (e.g. `"Content-Type: application/json"`) |
| `body` | `std::string` | Request body |

### RequestPolicy

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `timeout` | `float` | 0 | Total timeout in seconds (0 = no limit) |
| `connTimeout` | `float` | 0 | Connection timeout in seconds |
| `lowSpeedLimit` | `uint32_t` | 0 | Minimum transfer speed in bytes/sec |
| `lowSpeedTime` | `uint32_t` | 0 | Abort if below speed limit for this duration |
| `sendSpeedLimit` | `uint32_t` | 0 | Upload speed limit (0 = unlimited) |
| `recvSpeedLimit` | `uint32_t` | 0 | Download speed limit (0 = unlimited) |

### HttpResponse

| Field | Type | Description |
|-------|------|-------------|
| `status` | `long` | HTTP status code |
| `headers` | `std::vector<std::string>` | Response headers |
| `body` | `std::string` | Response body |
| `error` | `std::string` | Error message (if any) |
| `transferInfo` | `TransferInfo` | Timing information |

### TransferInfo

| Field | Type | Description |
|-------|------|-------------|
| `total` | `float` | Total transfer time in seconds |
| `connect` | `float` | Time to establish connection |
| `ttfb` | `float` | Time to first byte |
| `startTransfer` | `float` | Time until transfer started |

### HttpClient

| Method | Description |
|--------|-------------|
| `getInstance()` | Get singleton instance |
| `request(request, policy)` | Synchronous request |
| `request(request, policy, retryPolicy)` | Synchronous request with retry |
| `send_request(request, policy)` | Async request, returns `shared_ptr<TransferState>` |
| `send_request(request, policy, retryPolicy)` | Async request with retry |
| `stop()` | Stop client and cancel all pending requests |
| `uplinkSpeed()` / `downlinkSpeed()` | Average transfer speed in bytes/sec |
| `peakUplinkSpeed()` / `peakDownlinkSpeed()` | Peak transfer speed |

### TransferState

| Method | Description |
|--------|-------------|
| `future` | `std::shared_future<HttpResponse>` to get result |
| `cancel()` | Cancel the in-flight request |
| `pause()` | Pause the transfer |
| `resume()` | Resume a paused transfer |
| `get_state()` | Get current state: `Pending`, `Ongoing`, `Completed`, `Paused`, `Failed`, `Cancel` |
| `hasRetry()` | Check if retry policy is attached |
| `getAttempt()` | Get current attempt number |
| `getRetryContext()` | Get retry context with attempt history |

### RetryPolicy

| Field | Type | Description |
|-------|------|-------------|
| `maxRetries` | `uint32_t` | Maximum retry attempts (default: 3) |
| `totalTimeout` | `float` | Total timeout from first attempt in seconds (0 = no limit) |
| `shouldRetry` | `RetryConditionFn` | Function to determine if retry is needed |
| `getNextRetryTime` | `BackoffScheduleFn` | Function to calculate next retry time |

### Retry Conditions (retry namespace)

| Function | Description |
|----------|-------------|
| `defaultCondition()` | Retry on transient CURL errors |
| `httpStatusCondition(codes)` | Retry on specific HTTP status codes |
| `anyOf(conditions...)` | Combine conditions with OR logic |
| `allOf(conditions...)` | Combine conditions with AND logic |

### Backoff Strategies (retry namespace)

| Function | Description |
|----------|-------------|
| `exponentialBackoff(base, max, mult, jitter)` | Exponential backoff with jitter |
| `linearBackoff(initial, increment, max)` | Linear backoff |
| `fixedDelay(delay)` | Fixed delay between retries |
| `immediate()` | No delay (immediate retry) |
| `maxOf(strategies...)` | Use maximum delay from multiple strategies |
| `minOf(strategies...)` | Use minimum delay from multiple strategies |

## Configuration

Default settings in the library:

```cpp
#define MAX_CONNECTION 8L        // Max concurrent connections
#define POLL_MS 100L             // Multi poll timeout
#define SPEED_TRACK_WINDOW 128L  // Speed tracking window size
```

## License

See [LICENSE](LICENSE) file.
