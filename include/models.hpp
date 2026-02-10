#pragma once

#include "utils.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif
#include <curl/curl.h>
#ifdef __cplusplus
}
#endif

namespace http_client {

struct RequestPolicy {
	float timeout = 0;		// optional per-request timeout in seconds (<=0 means wait indefinitely)
	float connTimeout = 0;	// optional connection (DNS + handshake) timeout in seconds (<0 means default 300 second)

	uint32_t lowSpeedLimit = 0;	// in bytes
	uint32_t lowSpeedTime = 0;	// in seconds
	uint32_t sendSpeedLimit = 0;	// bytes per second
	uint32_t recvSpeedLimit = 0;	// bytes per second

	uint32_t curlBufferSize = CURL_MAX_WRITE_SIZE; // in bytes
};

// Fuck you, <winnt.h>
#ifdef _WIN32
#ifdef DELETE
#undef DELETE
#endif
#endif

struct HttpRequest {
public:
#define HTTP_METHODS                                                                                                   \
	HTTP_METHOD(GET)                                                                                                   \
	HTTP_METHOD(POST)                                                                                                  \
	HTTP_METHOD(HEAD)                                                                                                  \
	HTTP_METHOD(PATCH)                                                                                                 \
	HTTP_METHOD(PUT)                                                                                                   \
	HTTP_METHOD(DELETE)

	enum Method : uint8_t {
#define HTTP_METHOD(methodName) methodName,
		HTTP_METHODS
#undef HTTP_METHOD
			OTHER = 255
	};
	static constexpr std::string_view MethodStr[] = {
#define HTTP_METHOD(methodName) #methodName,
		HTTP_METHODS
#undef HTTP_METHOD
	};
	static Method method2Enum(const std::string& methodName) {
#define HTTP_METHOD(name)                                                                                              \
	if (util::toupper(methodName) == #name) {                                                                          \
		return Method::name;                                                                                           \
	}
		HTTP_METHODS
#undef HTTP_METHOD

		return Method::OTHER;
	};

	std::string url;
	std::string methodName;
	std::vector<std::string> headers; // e.g. "Content-Type: application/json"
	std::string body;				  // request body for POST
};

struct TransferInfo {
	// In second
	float startAt = std::chrono::duration<float>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	float queue = 0, connect = 0, appConnect = 0, preTransfer = 0, postTransfer = 0, ttfb = 0, startTransfer = 0, receiveTransfer = 0, total = 0, redir = 0;
	float completeAt = 0;
};

struct HttpResponse {
	long status = 0;

	std::vector<std::string> headers;
	std::string body;
	std::string error; // non-empty on error

	TransferInfo transferInfo;
};

} // namespace http_client
