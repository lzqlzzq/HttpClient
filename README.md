# HttpClient

A modern C++17 HTTP client library built on top of libcurl, featuring:

- blocking requests (`HttpTransfer`)
- async/concurrent requests with connection pooling (`HttpClient`)
- request control (`cancel/pause/resume`)
- configurable retry policy and backoff strategies
- built-in hash utilities (OpenSSL EVP wrappers)

## Features

- Blocking HTTP requests via `HttpTransfer`
- Async request queue and worker thread via `HttpClient`
- HTTP/2 capable (libcurl multi interface)
- Retry framework:
  - `RetryPolicy`
  - retry conditions (`retry::defaultCondition`, `retry::httpStatusCondition`, ...)
  - backoff strategies (`retry::exponentialBackoff`, `retry::fixedDelay`, ...)
- Transfer control:
  - `TransferState::cancel()`
  - `TransferState::pause()` / `TransferState::resume()`
- Speed tracking (`uplinkSpeed`, `downlinkSpeed`, peak variants)
- Hash helper APIs (`md5`, `sha1`, `sha2`, `sha3`, `blake2`, `sm3`, ...)

## Requirements

- C++17 compiler
- CMake >= 3.14
- OpenSSL
- libcurl >= 8.10.0 (if unavailable, vendored curl is fetched and built)

## Build

### Build library only

```bash
cmake -S . -B build
cmake --build build
```

### Build with example program

```bash
cmake -S . -B build -DHTTPCLIENT_BUILD_EXAMPLES=ON
cmake --build build
./build/example
```

Notes:

- The project option is `HTTPCLIENT_BUILD_EXAMPLES`.
- Vendored curl examples are explicitly disabled; only this project's `example` target is controlled by the option above.

## Integration

### Option 1: `add_subdirectory`

```cmake
add_subdirectory(path/to/HttpClient)
target_link_libraries(your_target PRIVATE HttpClient)
```

### Option 2: CMake `FetchContent`

```cmake
include(FetchContent)

FetchContent_Declare(
  HttpClient
  GIT_REPOSITORY https://github.com/your-org/HttpClient.git
  GIT_TAG main
)
FetchContent_MakeAvailable(HttpClient)

target_link_libraries(your_target PRIVATE HttpClient)
```

## Usage

### 1) Blocking request (`HttpTransfer`)

```cpp
#include "httpclient/HttpClient.hpp"

int main() {
    http_client::HttpRequest req;
    req.url = "https://httpbin.org/get";
    req.methodName = "GET";

    http_client::HttpTransfer transfer(req);
    transfer.perform_blocking();

    const auto& resp = transfer.getResponse();
    // resp.status / resp.body / resp.headers / resp.error
}
```

### 2) Async request (`HttpClient`)

```cpp
#include "httpclient/HttpClient.hpp"

auto& client = http_client::HttpClient::getDefault();

http_client::HttpRequest req;
req.url = "https://httpbin.org/get";
req.methodName = "GET";

auto state = client.send_request(req);
auto resp = state->future.get();
```

Or static convenience API:

```cpp
auto resp = http_client::HttpClient::Request(req);
```

### 3) Cancel / pause / resume

```cpp
auto state = http_client::HttpClient::SendRequest(req);

state->pause();
state->resume();
// or state->cancel();

auto resp = state->future.get();
```

### 4) Retry policy

```cpp
#include "httpclient/HttpClient.hpp"
#include "httpclient/RetryStrategies.hpp"

http_client::RetryPolicy retry;
retry.maxRetries = 3;
retry.totalTimeout = 30.0f;
retry.shouldRetry = http_client::retry::httpStatusCondition({500, 502, 503, 504});
retry.getNextRetryTime = http_client::retry::exponentialBackoff(1.0, 10.0, 2.0, 0.2);

auto resp = http_client::HttpClient::Request(
    req,
    http_client::RequestPolicy(),
    retry
);
```

`RetryPolicy()` default constructor is available and initializes:

- `maxRetries = 3`
- `totalTimeout = 0` (no total timeout)
- `shouldRetry = retry::defaultCondition()`
- `getNextRetryTime = retry::exponentialBackoff()`

### 5) Hash helper

```cpp
#include "httpclient/HashHelper.hpp"

std::string digest = http_client::Hash::sha256("hello");
```

## Public API Quick Reference

### `HttpRequest`

- `std::string url`
- `std::string methodName`
- `std::vector<std::string> headers`
- `std::string body`

### `RequestPolicy`

- `float timeout`
- `float connTimeout`
- `uint32_t lowSpeedLimit`
- `uint32_t lowSpeedTime`
- `uint32_t sendSpeedLimit`
- `uint32_t recvSpeedLimit`
- `uint32_t curlBufferSize`

### `HttpResponse`

- `long status`
- `std::vector<std::string> headers`
- `std::string body`
- `std::string error`
- `TransferInfo transferInfo`

### `TransferInfo`

Includes fine-grained timings such as:

- `startAt`, `queue`, `connect`, `appConnect`, `preTransfer`, `postTransfer`
- `ttfb`, `startTransfer`, `receiveTransfer`, `total`, `redir`, `completeAt`

### `HttpClient::TransferState`

- `std::shared_future<HttpResponse> future`
- `pause()`, `resume()`, `cancel()`
- `get_state()`
- `hasRetry()`, `getAttempt()`, `getRetryContext()`

State enum values:

- `Pending`, `Ongoing`, `Completed`, `Pause`, `Paused`, `Resume`, `Failed`, `Cancel`

### `RetryPolicy`

- `uint32_t maxRetries`
- `float totalTimeout`
- `RetryConditionFn shouldRetry`
- `BackoffScheduleFn getNextRetryTime`

## Retry Strategies

### Retry conditions (`namespace http_client::retry`)

- `defaultCondition()`
- `httpStatusCondition(codes)`
- `anyOf(...)`
- `allOf(...)`

### Backoff strategies (`namespace http_client::retry`)

- `exponentialBackoff(baseDelay, maxDelay, multiplier, jitterFactor)`
- `linearBackoff(initialDelay, increment, maxDelay)`
- `fixedDelay(delay)`
- `immediate()`
- `maxOf(...)`
- `minOf(...)`

## Notes

- Example program (`examples/example.cpp`) depends on internet access (uses `https://httpbin.org`).
- Header names are case-sensitive in this repo (for example, `Models.hpp`, `Utils.hpp`).

## License

See [LICENSE](LICENSE).
