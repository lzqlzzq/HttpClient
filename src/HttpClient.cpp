#include "HttpClient.hpp"
#include "RetryPolicy.hpp"
#include "curl/curl.h"
#include "curl/easy.h"

#include <atomic>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <regex>
#include <thread>

namespace http_client {

// HttpClientSettings implementation
const HttpClientSettings& HttpClientSettings::getDefault() {
	static HttpClientSettings defaultSettings;
	return defaultSettings;
}

void HttpClientSettings::applyCurlEasySettings(CURL* handle) const {
	curl_easy_setopt(handle, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
	curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
	curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 0L);
	curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(handle, CURLOPT_MAXCONNECTS, this->maxConnections);
	curl_easy_setopt(handle, CURLOPT_USE_SSL, CURLUSESSL_TRY);
}

void HttpClientSettings::applyCurlMultiSettings(CURLM* handle) const {
#if LIBCURL_VERSION_NUM >= 0x080c00
	curl_multi_setopt(handle, CURLMOPT_NETWORK_CHANGED, CURLMNWC_CLEAR_CONNS | CURLMNWC_CLEAR_DNS);
#endif
	curl_multi_setopt(handle, CURLMOPT_MAX_HOST_CONNECTIONS, this->maxHostConnections);
	curl_multi_setopt(handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, this->maxTotalConnections);
	curl_multi_setopt(handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
	curl_multi_setopt(handle, CURLMOPT_MAXCONNECTS, this->maxConnections);
}

inline static double current_time() {
	return std::chrono::duration<double>(
		std::chrono::system_clock::now().time_since_epoch()).count();
}

// HttpTransfer implementation
HttpTransfer::HttpTransfer(HttpRequest request, RequestPolicy policy, const HttpClientSettings& settings) :
	request(std::move(request)), policy(std::move(policy)), settings_(settings) {

	// static std::atomic<size_t> refCount_ = 0;
	static std::once_flag inited;

	std::call_once(inited, []() {
        auto rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK) throw std::runtime_error("curl_global_init failed");
        std::atexit([]{ curl_global_cleanup(); });
	});

	this->curlEasy = curl_easy_init();
	this->reset();
};

HttpTransfer::~HttpTransfer() {
	curl_easy_cleanup(this->curlEasy);
	curl_slist_free_all(this->headers_);
}

HttpTransfer::HttpTransfer(HttpTransfer&& other) noexcept
	: curlEasy(std::exchange(other.curlEasy, nullptr)),
	  headers_(std::exchange(other.headers_, nullptr)),
	  contentLength(other.contentLength),
	  request(std::move(other.request)),
	  response(std::move(other.response)),
	  policy(std::move(other.policy)),
	  settings_(other.settings_) {
	if (curlEasy) {
		curl_easy_setopt(curlEasy, CURLOPT_WRITEDATA, this);
		curl_easy_setopt(curlEasy, CURLOPT_HEADERDATA, this);
	}
}

HttpTransfer& HttpTransfer::operator=(HttpTransfer&& other) noexcept {
	if (this != &other) {
		// Clean up current resources
		curl_easy_cleanup(this->curlEasy);
		curl_slist_free_all(this->headers_);

		// Move from other
		this->curlEasy = std::exchange(other.curlEasy, nullptr);
		this->headers_ = std::exchange(other.headers_, nullptr);
		this->contentLength = other.contentLength;
		this->request = std::move(other.request);
		this->response = std::move(other.response);
		this->policy = std::move(other.policy);
		// Note: settings_ is a reference, cannot be reassigned

		// Update callback data pointers to this
		if (this->curlEasy) {
			curl_easy_setopt(this->curlEasy, CURLOPT_WRITEDATA, this);
			curl_easy_setopt(this->curlEasy, CURLOPT_HEADERDATA, this);
		}
	}
	return *this;
}

const HttpResponse& HttpTransfer::getResponse() const {
	return this->response;
}

HttpResponse HttpTransfer::detachResponse() {
	return std::move(this->response);
}

void HttpTransfer::finalize_transfer() {
	curl_easy_getinfo(this->curlEasy, CURLINFO_RESPONSE_CODE, &this->response.status);

	curl_off_t queue, connect, appConnect, preTransfer, postTransfer, startTransfer, total, redir;
	curl_easy_getinfo(this->curlEasy, CURLINFO_QUEUE_TIME_T, &queue);
	curl_easy_getinfo(this->curlEasy, CURLINFO_CONNECT_TIME_T, &connect);
	curl_easy_getinfo(this->curlEasy, CURLINFO_APPCONNECT_TIME_T, &appConnect);
	curl_easy_getinfo(this->curlEasy, CURLINFO_PRETRANSFER_TIME_T, &preTransfer);
	curl_easy_getinfo(this->curlEasy, CURLINFO_POSTTRANSFER_TIME_T, &postTransfer);
	curl_easy_getinfo(this->curlEasy, CURLINFO_STARTTRANSFER_TIME_T, &startTransfer);
	curl_easy_getinfo(this->curlEasy, CURLINFO_TOTAL_TIME_T, &total);
	curl_easy_getinfo(this->curlEasy, CURLINFO_REDIRECT_TIME_T, &redir);

	constexpr float us2s = 1e-6f;
	this->response.transferInfo.queue = queue * us2s;
	this->response.transferInfo.connect = (connect - queue) * us2s;
	this->response.transferInfo.appConnect = (appConnect - connect) * us2s;
	this->response.transferInfo.preTransfer = (preTransfer - appConnect) * us2s;
	this->response.transferInfo.postTransfer = (postTransfer - preTransfer) * us2s;
	this->response.transferInfo.startTransfer = (startTransfer - postTransfer) * us2s;
	this->response.transferInfo.receiveTransfer = (total - startTransfer) * us2s;
	this->response.transferInfo.total = total * us2s;
	this->response.transferInfo.redir = redir * us2s;

	this->response.transferInfo.completeAt = current_time();
}

void HttpTransfer::perform_blocking() {
	curl_easy_perform(this->curlEasy);
	this->finalize_transfer();
}

void HttpTransfer::reset() {
	if(!this->curlEasy)
		this->curlEasy = curl_easy_init();
	else
		curl_easy_reset(this->curlEasy);

	this->settings_.applyCurlEasySettings(this->curlEasy);

	curl_easy_setopt(this->curlEasy, CURLOPT_URL, this->request.url.c_str());
	if (this->policy.timeout > 0)
		curl_easy_setopt(this->curlEasy, CURLOPT_TIMEOUT_MS, static_cast<long>(this->policy.timeout * 1000));
	if (this->policy.connTimeout > 0)
		curl_easy_setopt(this->curlEasy, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(this->policy.connTimeout * 1000));
	if (this->policy.sendSpeedLimit)
		curl_easy_setopt(this->curlEasy, CURLOPT_MAX_SEND_SPEED_LARGE, this->policy.sendSpeedLimit);
	if (this->policy.recvSpeedLimit)
		curl_easy_setopt(this->curlEasy, CURLOPT_MAX_RECV_SPEED_LARGE, this->policy.recvSpeedLimit);
	if (this->policy.lowSpeedLimit && this->policy.lowSpeedTime) {
		curl_easy_setopt(this->curlEasy, CURLOPT_LOW_SPEED_TIME, this->policy.lowSpeedTime);
		curl_easy_setopt(this->curlEasy, CURLOPT_LOW_SPEED_LIMIT, this->policy.lowSpeedLimit);
	}
	if (this->policy.curlBufferSize) {
		unsigned long buf_size = std::clamp(this->policy.curlBufferSize, 1024u, static_cast<unsigned int>(CURL_MAX_READ_SIZE));
		curl_easy_setopt(this->curlEasy, CURLOPT_BUFFERSIZE, buf_size);
	}

	if(this->headers_)
		curl_slist_free_all(this->headers_);
	for (const auto header : this->request.headers) {
		this->headers_ = curl_slist_append(this->headers_, header.c_str());
	}
	curl_easy_setopt(this->curlEasy, CURLOPT_HTTPHEADER, this->headers_);

	switch (HttpRequest::method2Enum(this->request.methodName)) {
		case HttpRequest::HEAD: {
			curl_easy_setopt(this->curlEasy, CURLOPT_NOBODY, 1L);
			break;
		}
		case HttpRequest::GET: {
			curl_easy_setopt(this->curlEasy, CURLOPT_NOBODY, 1L);
			curl_easy_setopt(this->curlEasy, CURLOPT_HTTPGET, 1L);
			break;
		}
		case HttpRequest::POST: {
			curl_easy_setopt(this->curlEasy, CURLOPT_POST, 1L);
			curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDS, this->request.body.c_str());
			curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDSIZE, this->request.body.size());
			break;
		}
		default: {
			curl_easy_setopt(this->curlEasy, CURLOPT_CUSTOMREQUEST, util::toupper(this->request.methodName).c_str());
			if (this->request.body.size()) {
				curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDS, this->request.body.c_str());
				curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDSIZE, this->request.body.size());
			}
		}
	}

	curl_easy_setopt(this->curlEasy, CURLOPT_WRITEFUNCTION, HttpTransfer::body_cb);
	curl_easy_setopt(this->curlEasy, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(this->curlEasy, CURLOPT_HEADERFUNCTION, HttpTransfer::header_cb);
	curl_easy_setopt(this->curlEasy, CURLOPT_HEADERDATA, this);
}

size_t HttpTransfer::body_cb(void* ptr, size_t size, size_t nmemb, void* data) {
	HttpTransfer* transfer = static_cast<HttpTransfer*>(data);
	if(transfer->response.transferInfo.ttfb == 0)
		transfer->response.transferInfo.ttfb = current_time() - transfer->response.transferInfo.startAt;
	if(transfer->contentLength > transfer->response.body.capacity())
		transfer->response.body.reserve(transfer->contentLength);

	transfer->response.body.append((char*)ptr, size * nmemb);
	return size * nmemb;
}

size_t HttpTransfer::header_cb(void* ptr, size_t size, size_t nmemb, void* data) {
	HttpTransfer* transfer = static_cast<HttpTransfer*>(data);

	const size_t len = size * nmemb;
	if (!ptr || len == 0)
		return len;

	std::string_view sv(static_cast<const char*>(ptr), len);

	if (!sv.empty() && sv.back() == '\n')
		sv.remove_suffix(1);
	if (!sv.empty() && sv.back() == '\r')
		sv.remove_suffix(1);

	if (sv.empty())
		return len;
	if (sv.rfind("HTTP/", 0) == 0)
		return len;

	transfer->response.headers.emplace_back(sv);

	// Parse content-length for pre-allocation
	static const std::regex contentLengthRegex("^content-length:\\s*(\\d+)", std::regex::icase);
	std::match_results<std::string_view::const_iterator> match;
	if (std::regex_search(sv.begin(), sv.end(), match, contentLengthRegex)) {
		transfer->contentLength = std::atoi(match[1].str().c_str());
	}

	return len;
}

// HttpClient::TransferState implementation
HttpClient::TransferState::TransferState(std::shared_future<HttpResponse>&& future, CURL* curl, HttpClient* client)
	: future(future), curl(curl), client_(client) {}

void HttpClient::TransferState::cancel() {
	state.store(State::Cancel, std::memory_order_release);

	{
		std::unique_lock<std::mutex> lk(client_->mutex_);
		client_->events_.emplace(this->curl);
	}
	curl_multi_wakeup(client_->multi_);
}

void HttpClient::TransferState::pause() {
	State expected = State::Ongoing;
	if (!state.compare_exchange_strong(expected, State::Pause, std::memory_order_acq_rel)) {
		// If not from Ongoing, discard
		return;
	}

	{
		std::unique_lock<std::mutex> lk(client_->mutex_);
		client_->events_.emplace(this->curl);
	}
	curl_multi_wakeup(client_->multi_);
}

void HttpClient::TransferState::resume() {
	State expected = State::Paused;
	if (!state.compare_exchange_strong(expected, State::Resume, std::memory_order_acq_rel)) {
		// If not from Paused, discard
		return;
	}

	{
		std::unique_lock<std::mutex> lk(client_->mutex_);
		client_->events_.emplace(this->curl);
	}
	curl_multi_wakeup(client_->multi_);
}

HttpClient::TransferState::State HttpClient::TransferState::get_state() {
	return this->state.load(std::memory_order_acquire);
}

bool HttpClient::TransferState::hasRetry() const {
	return retry_ != nullptr;
}

uint32_t HttpClient::TransferState::getAttempt() const {
	return retry_ ? retry_->context.attemptCount() : 0;
}

std::optional<std::reference_wrapper<RetryContext>> HttpClient::TransferState::getRetryContext() const {
	std::optional<std::reference_wrapper<RetryContext>> context;
	if(this->retry_) context.emplace(this->retry_->context);
	return context;
}

// HttpClient::TransferTask implementation
HttpClient::TransferTask::TransferTask(HttpRequest r, RequestPolicy p, HttpClient* client)
	: transfer(r, p, client->settings_), policy(p), state(std::shared_ptr<TransferState>(
					   new TransferState(this->promise.get_future().share(), this->transfer.curlEasy, client))) {};

HttpClient::TransferTask::TransferTask(HttpRequest r, RequestPolicy p, RetryPolicy retryPolicy, HttpClient* client)
	: transfer(r, p, client->settings_), policy(p), state(std::shared_ptr<TransferState>(
					   new TransferState(this->promise.get_future().share(), this->transfer.curlEasy, client))) {
	state->retry_ = std::make_unique<TransferState::RetryState>();
	state->retry_->policy = std::move(retryPolicy);
	state->retry_->context.first_attempt_at = current_time();
};

// HttpClient implementation
HttpClient& HttpClient::getDefault() {
	static HttpClient instance;
	return instance;
}

void HttpClient::stop() {
	if (!this->stop_.load()) {
		this->stop_.store(true);
		curl_multi_wakeup(this->multi_);
	}
}

void HttpClient::init() {
	this->multi_ = curl_multi_init();
	this->settings_.applyCurlMultiSettings(this->multi_);
	this->worker_ = std::thread(&HttpClient::worker_loop, this);
}

HttpClient::HttpClient()
	: settings_(HttpClientSettings::getDefault()),
	  sema_(settings_.maxConnections, settings_.maxConnections),
	  uplinkAvgSpeed(settings_.speedTrackWindow),
	  downlinkAvgSpeed(settings_.speedTrackWindow) {
	init();
}

HttpClient::HttpClient(const HttpClientSettings& settings)
	: settings_(settings),
	  sema_(settings_.maxConnections, settings_.maxConnections),
	  uplinkAvgSpeed(settings_.speedTrackWindow),
	  downlinkAvgSpeed(settings_.speedTrackWindow) {
	init();
}

HttpClient::~HttpClient() {
	this->stop();
	if (this->worker_.joinable())
		this->worker_.join();

	curl_multi_cleanup(this->multi_);
}

float HttpClient::uplinkSpeed() const {
	return this->uplinkAvgSpeed.mean();
}

float HttpClient::downlinkSpeed() const {
	return this->downlinkAvgSpeed.mean();
}

float HttpClient::peakUplinkSpeed() const {
	return this->uplinkAvgSpeed.max();
}

float HttpClient::peakDownlinkSpeed() const {
	return this->downlinkAvgSpeed.max();
}

HttpResponse HttpClient::request(HttpRequest request, RequestPolicy policy) {
	std::shared_ptr<TransferState> state = this->send_request(std::move(request), std::move(policy));

	return state->future.get();
}

std::shared_ptr<HttpClient::TransferState> HttpClient::send_request(HttpRequest request, RequestPolicy policy) {
	TransferTask task(std::move(request), std::move(policy), this);
	std::shared_ptr<TransferState> state = task.state;

	this->sema_.acquire();
	std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(std::abs(util::jitter_generator(10))));

	{
		std::unique_lock lk(this->mutex_);
		this->requests.emplace(std::move(task));
	}
	curl_multi_wakeup(this->multi_);

	return state;
}

HttpResponse HttpClient::request(HttpRequest request, RequestPolicy policy, RetryPolicy retryPolicy) {
	std::shared_ptr<TransferState> state = this->send_request(std::move(request), std::move(policy), std::move(retryPolicy));

	return state->future.get();
}

std::shared_ptr<HttpClient::TransferState> HttpClient::send_request(HttpRequest request, RequestPolicy policy, RetryPolicy retryPolicy) {
	TransferTask task(std::move(request), std::move(policy), std::move(retryPolicy), this);
	std::shared_ptr<TransferState> state = task.state;

	this->sema_.acquire();
	std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(std::abs(util::jitter_generator(10))));

	{
		std::unique_lock lk(this->mutex_);
		this->requests.emplace(std::move(task));
	}
	curl_multi_wakeup(this->multi_);

	return state;
}

void HttpClient::worker_loop() {
	while (1) {
		int still_running = 0;
		CURLMcode mc;
		do {
			mc = curl_multi_perform(multi_, &still_running);
		} while (mc == CURLM_CALL_MULTI_PERFORM);

		// Harvest results
		CURLMsg* msg;
		do {
			int msgq = 0;
			msg = curl_multi_info_read(this->multi_, &msgq);
			if (msg && (msg->msg == CURLMSG_DONE)) {
				CURL* easy = msg->easy_handle;
				curl_multi_remove_handle(this->multi_, easy);
				this->sema_.release();

				auto mit = this->curl2Task.find(easy);
				if (mit != this->curl2Task.end() && mit->second) {
					auto it = mit->second.value();
					CURLcode curlCode = msg->data.result;

					it->transfer.finalize_transfer();

					// Record uplink and downlink speed
					curl_off_t upSpeed;
					curl_off_t dlSpeed;
					curl_easy_getinfo(easy, CURLINFO_SPEED_UPLOAD_T, &upSpeed);
					curl_easy_getinfo(easy, CURLINFO_SPEED_DOWNLOAD_T, &dlSpeed);
					this->downlinkAvgSpeed.push(dlSpeed);
					this->uplinkAvgSpeed.push(upSpeed);

					if (it->state->retry_) {
						this->handle_retry_completion(it, curlCode);
					} else {
						// Non-retry request: set promise value and cleanup
						it->promise.set_value(std::move(it->transfer.detachResponse()));
						it->state->state.store(TransferState::State::Completed, std::memory_order_release);
						this->handle_completion(it);
					}
				}
			}
		} while (msg);

		long t = -1;
		curl_multi_timeout(multi_, &t);

		int poll_timeout;
		if (t < 0)
			poll_timeout = POLL_MS;
		else if (t == 0)
			poll_timeout = 0;
		else
			poll_timeout = (int)std::min<long>(t, POLL_MS);

		// Handle retry - only process retries whose time has come
		while(!this->pendingRetries_.empty()) {
			double now = current_time();
			double deltaTime = this->pendingRetries_.top().retryAt - now;

			if(deltaTime <= 0 && sema_.try_acquire()) {
				auto&& top = const_cast<TransferTask&>(this->pendingRetries_.top());
				TransferTask task = std::move(top);
				this->pendingRetries_.pop();

				task.transfer.reset();

				std::unique_lock lk(this->mutex_);
				this->requests.emplace(std::move(task));
			} else {
				// Adjust poll_timeout to wake up when next retry is due
				int retryWaitMs = static_cast<int>(std::max(0.0, deltaTime) * 1000);
				poll_timeout = std::min(poll_timeout, retryWaitMs);
				break;
			}
		}

		curl_multi_poll(multi_, nullptr, 0, poll_timeout, NULL);

		// Handle stop
		if (this->stop_.load()) [[unlikely]] {
			std::unique_lock<std::mutex> lk(this->mutex_);

			for (auto it = transfers.begin(); it != transfers.end(); ++it) {
				curl_multi_remove_handle(this->multi_, it->transfer.curlEasy);
				it->promise.set_exception(
					std::make_exception_ptr(std::runtime_error("The HttpClient stopped while task in the pool.")));
			}

			// Exit the worker loop
			break;
		}

		// Handle events
		this->handle_events();

		// Add new request
		std::vector<TransferTask> pendingTasks;
		{
			std::unique_lock<std::mutex> lk(this->mutex_);

			pendingTasks.reserve(this->requests.size());
			while (!this->requests.empty()) {
				pendingTasks.emplace_back(std::move(this->requests.front()));
				this->requests.pop();
			}
		}

		for (auto&& task : pendingTasks) {
			this->transfers.emplace_back(std::move(task));
			auto it = std::prev(this->transfers.end());
			this->curl2Task[this->transfers.back().transfer.curlEasy] = it;

			curl_multi_add_handle(this->multi_, this->transfers.back().transfer.curlEasy);
		}
	}
}

void HttpClient::handle_events() {
	std::vector<CURL*> events;
	events.reserve(this->events_.size());
	{
		std::lock_guard<std::mutex> lk(this->mutex_);
		while (!this->events_.empty()) {
			events.push_back(this->events_.front());
			this->events_.pop();
		}
	}

	for (CURL* curlEasy : events) {
		if (!curlEasy)
			continue;

		auto mit = this->curl2Task.find(curlEasy);
		if (mit == this->curl2Task.end() || !mit->second)
			continue;

		auto it = mit->second.value();
		TransferState::State state = it->state->state.load(std::memory_order_acquire);

		switch (state) {
			case TransferState::Cancel:
				this->handle_cancel(*it);
				this->curl2Task.erase(curlEasy);
				this->transfers.erase(it);
				break;
			case TransferState::Pause:
				this->handle_pause(*it);
				break;
			case TransferState::Resume:
				this->handle_resume(*it);
				break;
			default:
				break;
		}
	}
}

void HttpClient::handle_cancel(TransferTask& task) {
	curl_multi_remove_handle(this->multi_, task.transfer.curlEasy);
	this->sema_.release();
	task.promise.set_exception(std::make_exception_ptr(std::runtime_error("The task is cancelled.")));
}

void HttpClient::handle_pause(TransferTask& task) {
	curl_easy_pause(task.transfer.curlEasy, CURLPAUSE_ALL);
	task.state->state.store(TransferState::Paused, std::memory_order_release);
	this->sema_.release();
}

void HttpClient::handle_resume(TransferTask& task) {
	// Acquire semaphore before resuming
	if (!this->sema_.try_acquire()) {
		// Re-queue the event for next epoch
		std::lock_guard<std::mutex> lk(this->mutex_);
		this->events_.emplace(task.transfer.curlEasy);
		return;
	}

	curl_easy_pause(task.transfer.curlEasy, CURLPAUSE_CONT);
	task.state->state.store(TransferState::Ongoing, std::memory_order_release);
}

void HttpClient::handle_retry_completion(std::list<TransferTask>::iterator it, CURLcode curlCode) {
	auto& context = it->state->retry_->context;
	auto& policy = it->state->retry_->policy;

	double now = current_time();
	context.attempts.emplace_back(AttemptRecord{
		it->transfer.getResponse(),
		curlCode, now});

	if (policy.shouldRetry && policy.shouldRetry(context) &&
		context.attemptCount() < policy.maxRetries &&
		((policy.totalTimeout <= 0) ||
	    (now - context.first_attempt_at < policy.totalTimeout))) {
		it->retryAt = policy.getNextRetryTime(context);

		this->curl2Task.erase(it->transfer.curlEasy);
		this->pendingRetries_.emplace(std::move(*it));
		this->transfers.erase(it);
	} else {
		// No need for retry - complete the request
		it->promise.set_value(std::move(it->transfer.detachResponse()));
		it->state->state.store(TransferState::State::Completed, std::memory_order_release);
		this->handle_completion(it);
	}
}

void HttpClient::handle_completion(std::list<TransferTask>::iterator it) {
	this->curl2Task.erase(it->transfer.curlEasy);
	this->transfers.erase(it);
}

} // namespace http_client
