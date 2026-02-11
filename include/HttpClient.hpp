#pragma once

#include "models.hpp"
#include "utils.hpp"
#include "RetryPolicy.hpp"

#include <atomic>
#include <future>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <thread>

namespace http_client {

#define MAX_CONNECTION 8L
#define POLL_MS 100L
#define SPEED_TRACK_WINDOW 128L

void CURL_EASY_DEFAULT_SETTING(CURL* handle);
void CURL_MULTI_DEFAULT_SETTING(CURLM* handle);

class HttpTransfer {
public:
	explicit HttpTransfer(HttpRequest request, RequestPolicy policy=RequestPolicy());
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
	void reset();

private:
	friend class HttpClient;

	CURL* curlEasy = NULL;
	struct curl_slist* headers_ = NULL;
	size_t contentLength = -1;
	HttpRequest request;
	HttpResponse response;
	RequestPolicy policy;

	static size_t body_cb(void* ptr, size_t size, size_t nmemb, void* data);
	static size_t header_cb(void* ptr, size_t size, size_t nmemb, void* data);
};

class HttpClient {
public:
	class TransferState {
	public:
		enum State { Pending, Ongoing, Completed, Pause, Paused, Resume, Failed, Cancel };
		std::shared_future<HttpResponse> future;

		// Basic accessors
		void pause();
		void resume();
		void cancel();
		State get_state();

		// Retry-related accessors
		bool hasRetry() const;
		uint32_t getAttempt() const;
		std::optional<std::reference_wrapper<RetryContext>> getRetryContext() const;

	private:
		struct RetryState {
			RetryContext context;
			RetryPolicy policy;
		};

		std::atomic<State> state{State::Ongoing};
		CURL* curl;
		std::unique_ptr<RetryState> retry_;

		explicit TransferState(std::shared_future<HttpResponse>&& future, CURL* curl);

		friend class HttpClient;
	};

	static HttpClient& getInstance();

	void stop();

	HttpResponse request(HttpRequest request, RequestPolicy policy=RequestPolicy());
	std::shared_ptr<TransferState> send_request(HttpRequest request, RequestPolicy policy=RequestPolicy());

	HttpResponse request(HttpRequest request, RequestPolicy policy, RetryPolicy retryPolicy);
	std::shared_ptr<TransferState> send_request(HttpRequest request, RequestPolicy policy, RetryPolicy retryPolicy);

	float uplinkSpeed() const;
	float downlinkSpeed() const;

	float peakUplinkSpeed() const;
	float peakDownlinkSpeed() const;

private:
	HttpClient();
	~HttpClient();

	class TransferTask {
	private:
		HttpTransfer transfer;
		RequestPolicy policy;
		std::promise<HttpResponse> promise;
		std::shared_ptr<TransferState> state;

		double retryAt = 0;

		explicit TransferTask(HttpRequest r, RequestPolicy p);
		explicit TransferTask(HttpRequest r, RequestPolicy p, RetryPolicy retryPolicy);

		friend class HttpClient;
	};

	void worker_loop();
	void handle_events();
	void handle_cancel(TransferTask& task);
	void handle_pause(TransferTask& task);
	void handle_resume(TransferTask& task);

	void handle_completion(std::list<TransferTask>::iterator it);
	void handle_retry_completion(std::list<TransferTask>::iterator it, CURLcode curlCode);

	using TaskIter = std::optional<std::list<TransferTask>::iterator>;

	std::thread worker_;

	std::queue<TransferTask> requests;
	std::list<TransferTask> transfers;
	std::map<CURL*, TaskIter> curl2Task;
	std::queue<CURL*> events_;

	struct RetryCompare {
		bool operator()(const TransferTask& a,
		                const TransferTask& b) const {
			return a.retryAt > b.retryAt;  // Earlier time = higher priority
		}
	};

	std::priority_queue<TransferTask,
	                    std::vector<TransferTask>,
	                    RetryCompare> pendingRetries_;

	CURLM* multi_;

	std::atomic<bool> stop_{false};
	std::mutex mutex_;
	BoundedSemaphore sema_;

	SlidingWindow<float> uplinkAvgSpeed;
	SlidingWindow<float> downlinkAvgSpeed;

	friend class TransferState;
};

} // namespace http_client
