# HttpClient

`HttpClient` is a C++17 HTTP client library built on top of libcurl multi/easy APIs.

It provides:

- blocking requests via `HttpTransfer`
- async/concurrent requests via pooled `HttpClient`
- runtime transfer control (`cancel`, `pause`, `resume`)
- pluggable retry condition + backoff scheduling
- hashing helpers based on OpenSSL EVP

## Requirements

- C++17 compiler
- CMake >= 3.14
- OpenSSL
- libcurl >= 8.10.0 (or vendored curl auto-built by CMake)

## Build

### Library only

```bash
cmake -S . -B build
cmake --build build
```

### Build example

```bash
cmake -S . -B build -DHTTPCLIENT_BUILD_EXAMPLES=ON
cmake --build build
./build/example
```

Notes:

- `HTTPCLIENT_BUILD_EXAMPLES` controls only `examples/example.cpp`.
- If system libcurl is missing or too old, CMake fetches and builds `curl-8_11_1` with HTTP-only options.

## Integrate In Your Project

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
  GIT_REPOSITORY https://github.com/lzqlzzq/HttpClient.git
  GIT_TAG main
)
FetchContent_MakeAvailable(HttpClient)

target_link_libraries(your_target PRIVATE HttpClient)
```

## Usage

### Blocking request (`HttpTransfer`)

```cpp
#include "httpclient/HttpClient.hpp"

int main() {
    http_client::HttpRequest req;
    req.url = "https://httpbin.org/get";
    req.methodName = "GET";

    http_client::HttpTransfer transfer(req);
    transfer.perform_blocking();

    const auto& resp = transfer.getResponse();
    // resp.status / resp.headers / resp.body / resp.error
}
```

### Async request (`HttpClient`)

```cpp
#include "httpclient/HttpClient.hpp"

auto& client = http_client::HttpClient::getDefault();

http_client::HttpRequest req;
req.url = "https://httpbin.org/get";
req.methodName = "GET";

auto state = client.send_request(req);
auto resp = state->future.get();
```

Static convenience API:

```cpp
auto resp = http_client::HttpClient::Request(req);
```

### Cancel / pause / resume

```cpp
auto state = http_client::HttpClient::SendRequest(req);
state->pause();
state->resume();
// state->cancel();
auto resp = state->future.get();
```

### Retry policy

```cpp
#include "httpclient/HttpClient.hpp"
#include "httpclient/RetryStrategies.hpp"

http_client::RetryPolicy retry;
retry.maxRetries = 3;
retry.totalTimeout = 30.0f;
retry.shouldRetry = http_client::retry::httpStatusCondition({500, 502, 503, 504});
retry.getNextRetryTime = http_client::retry::exponentialBackoff(1.0, 10.0, 2.0, 0.2);

auto resp = http_client::HttpClient::Request(req, http_client::RequestPolicy(), retry);
```

Default `RetryPolicy()`:

- `maxRetries = 3`
- `totalTimeout = 0` (unbounded total time)
- `shouldRetry = retry::defaultCondition()`
- `getNextRetryTime = retry::exponentialBackoff()`

### Hash helper

```cpp
#include "httpclient/HashHelper.hpp"

std::string bin = http_client::Hash::sha256("hello");
std::string hex = http_client::Hash::hexdigest(bin);
```

## API Summary

### `HttpRequest`

- `url`
- `methodName` (e.g. `GET`, `POST`, `PATCH`, `PUT`, `DELETE`, `HEAD`)
- `headers`
- `body`

### `RequestPolicy`

- `timeout` (seconds)
- `connTimeout` (seconds)
- `lowSpeedLimit`, `lowSpeedTime`
- `sendSpeedLimit`, `recvSpeedLimit`
- `curlBufferSize`

### `HttpResponse`

- `status`
- `headers`
- `body`
- `error` (curl error buffer)
- `transferInfo` (timing breakdown)

### `HttpClient::TransferState`

- `future`
- `pause()`, `resume()`, `cancel()`
- `get_state()`
- `hasRetry()`, `getAttempt()`, `getRetryContext()`

State values:

- `Pending`, `Ongoing`, `Completed`, `Pause`, `Paused`, `Resume`, `Failed`, `Cancel`

### Retry helpers (`namespace http_client::retry`)

Conditions:

- `defaultCondition()`
- `httpStatusCondition(codes)`
- `anyOf(...)`
- `allOf(...)`

Backoff:

- `exponentialBackoff(baseDelay, maxDelay, multiplier, jitterFactor)`
- `linearBackoff(initialDelay, increment, maxDelay)`
- `fixedDelay(delay)`
- `immediate()`
- `maxOf(...)`
- `minOf(...)`

## Runtime Notes

- Set `HTTPCLIENT_CURL_VERBOSE=1` to enable libcurl verbose output for requests.
- `HttpClientSettings` can be subclassed to override curl easy/multi options.
- If you construct `HttpClient` with a custom `HttpClientSettings` reference, ensure the settings object outlives the client.
- `examples/example.cpp` requires internet access (`https://httpbin.org`).

## License

See [LICENSE](LICENSE).
