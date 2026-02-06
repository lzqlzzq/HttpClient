#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

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
#define SPEED_TRACK_WINDOW 128L

void CURL_EASY_DEFAULT_SETTING(CURL* handle);
void CURL_MULTI_DEFAULT_SETTING(CURLM* handle);

static std::string toupper(const std::string& str) {
	std::string s(str);
	for (char& c : s)
		if (c >= 'a' && c <= 'z')
			c -= 32;
	return s;
};

template <typename T, class = std::enable_if_t<std::is_arithmetic<T>::value>> class SlidingAvg {
public:
	explicit SlidingAvg(size_t capacity) : cap(capacity) {
		this->buffer.reserve(capacity);
	};

	void push(T value) {
		if (this->size < this->cap) [[__unlikely__]] {
			// Not full
			this->buffer[this->head_] = value;
			this->sum_ += value;
			++this->size;
			this->head_ = (this->head_ + 1) % this->cap;
		} else {
			// Full
			this->sum_ -= this->buffer[this->head_];
			this->buffer[this->head_] = value;
			this->sum_ += value;
			this->head_ = (this->head_ + 1) % this->cap;
		}
	}

	double mean() const {
		return this->size ? static_cast<double>(sum_) / this->size : 0.0;
	}

	void clear() {
		this->head_ = 0;
		this->size = 0;
		std::fill(this->buffer.begin(), this->buffer.end(), 0);
	};

private:
	std::vector<T> buffer;
	size_t size = 0;
	size_t cap = 0;
	size_t head_ = 0;

	double sum_ = 0.0;
};

struct RequestPolicy {
	uint64_t timeout_ms = 0;		// optional per-request timeout (<=0 means wait indefinitely)
	uint64_t conn_timeout_ms = 0; // optional connection (DNS + handshake) timeout (<0 means default 300 second)

	uint64_t low_speed_limit = 0; // in byte
	uint64_t low_speed_time = 0;  // in second
	uint64_t send_speed_limit = 0; // bytes per second
	uint64_t recv_speed_limit = 0; // bytes per second
};

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
	if (toupper(methodName) == #name) {                                                                                         \
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
	// In microsecond
	uint64_t queue_s, connect_s, appconnect_s, pretransfer_s, posttransfer_s, starttransfer_s, receivetransfer_s, total_s, redir_s;
};

struct HttpResponse {
	long status = 0;

	std::vector<std::string> headers;
	std::string body;
	std::string error; // non-empty on error

	TransferInfo transferInfo;
};

class HttpTransfer {
public:
	explicit HttpTransfer(const HttpRequest& request, const RequestPolicy& policy=RequestPolicy());
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
	RequestPolicy policy;

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

	// Acquire the semaphore (Non-blocking)
	bool try_acquire();

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
		enum State { Pending, Ongoing, Completed, Pause, Paused, Failed, Cancel };
		std::shared_future<HttpResponse> future;

		void pause();
		void resume();
		void cancel();
		State get_state();

	private:
		std::atomic<State> state = State::Ongoing;
		CURL* curl; // Only for look-up

		explicit TransferState(std::shared_future<HttpResponse>&& future, CURL* curl);

		friend class HttpClient;
	};

	static HttpClient& getInstance();

	void stop();

	HttpResponse request(HttpRequest request, RequestPolicy policy=RequestPolicy());
	std::shared_ptr<TransferState> send_request(HttpRequest request, RequestPolicy policy=RequestPolicy());

	float uplinkSpeed() const;
	float downlinkSpeed() const;

private:
	HttpClient();
	~HttpClient();

	void worker_loop();

	class TransferTask {
	private:
		HttpTransfer transfer;
		RequestPolicy policy;
		std::promise<HttpResponse> promise;
		std::shared_ptr<TransferState> state;

		explicit TransferTask(HttpRequest r, RequestPolicy p);

		friend class HttpClient;
	};

	using TaskIter = std::optional<std::list<TransferTask>::iterator>;

	std::thread worker_;

	std::queue<TransferTask> requests;
	std::list<TransferTask> transfers;
	std::map<CURL*, TaskIter> curl2Task;
	std::queue<CURL*> toCancelled;
	std::queue<CURL*> toPaused;
	std::queue<CURL*> toResumed;

	CURLM* multi_;

	std::atomic<bool> stop_{false};
	std::mutex mutex_;
	BoundedSemaphore sema_;

	SlidingAvg<float> uplinkAvgSpeed;
	SlidingAvg<float> downlinkAvgSpeed;

	friend class TransferState;
};

float jitter_generator(float max);    // In second

} // namespace http_client
