#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#ifdef __cplusplus
extern "C" {
#endif
#include <curl/curl.h>
#ifdef __cplusplus
}
#endif

// Fuck you, <winnt.h>
#ifdef _WIN32
#ifdef DELETE
#undef DELETE
#endif
#endif

namespace http_client {

#define MAX_CONNECTION 8L
#define POLL_MS 100L

void CURL_EASY_DEFAULT_SETTING(CURL* handle);
void CURL_MULTI_DEFAULT_SETTING(CURLM* handle);

class HttpRequest {
public:
#define HTTP_METHODS   \
	HTTP_METHOD(GET)   \
	HTTP_METHOD(POST)  \
	HTTP_METHOD(HEAD)  \
	HTTP_METHOD(PATCH) \
	HTTP_METHOD(PUT)   \
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
	static constexpr Method method2Enum(const std::string& methodName) {
#define HTTP_METHOD(name) \
	if (methodName == #name) { return Method::name; }
		HTTP_METHODS
#undef HTTP_METHOD

		return Method::OTHER;
	};

	HttpRequest() = default;

	HttpRequest(
		std::string url,
		std::string methodName,
		long conn_timeout_ms = 0,
		long timeout_ms = 0,
		std::vector<std::string> headers = {},
		std::string body = {}) :
		url(url),
		methodName(methodName),
		method(method2Enum(methodName)),
		timeout_ms(timeout_ms),
		conn_timeout_ms(conn_timeout_ms),
		headers(headers),
		body(body) {};

	std::string url;
	std::string methodName;
	Method method;
	std::vector<std::string> headers;  // e.g. "Content-Type: application/json"
	std::string body;				   // request body for POST

	const long timeout_ms = 0;  // optional per-request timeout (<=0 means wait indefinitely)
	const long conn_timeout_ms = 0;  // optional connection (DNS+handshake) timeout (<=0 means wait indefinitely)
};

struct HttpResponse {
	long status = 0;

	std::vector<std::string> headers;
	std::string body;
	std::string error;// non-empty on error

	double elapsed;
};

class HttpTransfer {
public:
	explicit HttpTransfer(const HttpRequest& request);
	~HttpTransfer();

	// Moveable, Not copyable
	HttpTransfer(const HttpTransfer&) = delete;
	HttpTransfer& operator=(const HttpTransfer&) = delete;
	HttpTransfer(HttpTransfer&& other) noexcept;
	HttpTransfer& operator=(HttpTransfer&& other) noexcept;

	const HttpResponse& getResponse() const;
	HttpResponse detachResponse();
	void finalize_transfer();
	void perform_blocking();

private:
	friend class HttpClient;

	CURL* curlEasy = NULL;
	struct curl_slist* headers_ = NULL;
	HttpResponse response;
	std::string url;

	static size_t body_cb(void* ptr, size_t size, size_t nmemb, std::string* data);
	static size_t header_cb(void* ptr, size_t size, size_t nmemb, std::vector<std::string>* data);
};

class BoundedSemaphore {
public:
	explicit BoundedSemaphore(size_t initial_count, size_t max_count);

	// non-copyable
	BoundedSemaphore(const BoundedSemaphore&) = delete;

	// Acquire the semaphore (P operation)
	void acquire();

	// Release the semaphore (V operation)
	void release();

private:
	std::mutex mutex_;
	std::condition_variable cv_;
	unsigned int count_;
	const unsigned int max_count_;
};

class HttpClient {
public:

	class TransferState {
	public:
		enum State {
			Ongoing, Completed, Cancel
		};
		std::shared_future<HttpResponse> future;

		void cancel();
		State get_state();

	private:
		std::atomic<State> state = State::Ongoing;
		CURL* curl;  // Only for look-up

		explicit TransferState(std::shared_future<HttpResponse>&& future, CURL* curl);

		friend class HttpClient;
	};

	static HttpClient& getInstance();

	void stop();

	template<class... U>
	HttpResponse request(U&&... req) {
		std::shared_ptr<TransferState> state = this->send_request(std::forward<U>(req)...);

		return state->future.get();
	};

	template<class... U>
	std::shared_ptr<TransferState> send_request(U&&... req) {
		TransferTask task(HttpRequest{std::forward<U>(req)...});
		std::shared_ptr<TransferState> state = task.state;

		this->sema_.acquire();
		{
			std::unique_lock lk(this->mutex_);
			this->requests.emplace(std::move(task));
		}
		curl_multi_wakeup(this->multi_);

		return state;
	};

private:
	HttpClient();
	~HttpClient();

	void worker_loop();

	class TransferTask {
	private:
		HttpTransfer transfer;
		std::promise<HttpResponse> promise;
		std::shared_ptr<TransferState> state;

		explicit TransferTask(HttpRequest r);

		friend class HttpClient;
	};

	using TaskIter = std::optional<std::list<TransferTask>::iterator>;

	std::thread worker_;

	std::queue<TransferTask> requests;
	std::list<TransferTask> transfers;
	std::map<CURL*, TaskIter> curl2Task;
	std::queue<CURL*> toCancelled;

	CURLM* multi_;

	std::atomic<bool> stop_{false};
	std::mutex mutex_;
	BoundedSemaphore sema_;

	friend class TransferState;
};

}// namespace http_client
