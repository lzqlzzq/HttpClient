#include "HttpClient.hpp"

#include <atomic>
#include <cstdlib>
#include <regex>
#include <thread>
#include <random>

namespace http_client {

void CURL_EASY_DEFAULT_SETTING(CURL* handle) {
	curl_easy_setopt(handle, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
	curl_easy_setopt(handle, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_NONE);
	curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 0L);
	curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(handle, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(handle, CURLOPT_MAXCONNECTS, 8L);
	curl_easy_setopt(handle, CURLOPT_USE_SSL, CURLUSESSL_TRY);
}

void CURL_MULTI_DEFAULT_SETTING(CURLM* handle) {
#if LIBCURL_VERSION_NUM >= 0x080c00
	curl_multi_setopt(handle, CURLMOPT_NETWORK_CHANGED, CURLMNWC_CLEAR_CONNS | CURLMNWC_CLEAR_DNS);
#endif
	curl_multi_setopt(handle, CURLMOPT_MAX_HOST_CONNECTIONS, 2L);
	curl_multi_setopt(handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, 4L);
	curl_multi_setopt(handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
	curl_multi_setopt(handle, CURLMOPT_MAXCONNECTS, 8L);
}

// HttpTransfer implementation
HttpTransfer::HttpTransfer(const HttpRequest& request, const RequestPolicy& policy) {
	this->curlEasy = curl_easy_init();

	CURL_EASY_DEFAULT_SETTING(this->curlEasy);

	curl_easy_setopt(this->curlEasy, CURLOPT_URL, request.url.c_str());
	if (policy.timeout_ms > 0)
		curl_easy_setopt(this->curlEasy, CURLOPT_TIMEOUT_MS, policy.timeout_ms);
	if (policy.conn_timeout_ms > 0)
		curl_easy_setopt(this->curlEasy, CURLOPT_CONNECTTIMEOUT_MS, policy.conn_timeout_ms);
	if (policy.send_speed_limit)
		curl_easy_setopt(this->curlEasy, CURLOPT_MAX_SEND_SPEED_LARGE, policy.send_speed_limit);
	if (policy.recv_speed_limit)
		curl_easy_setopt(this->curlEasy, CURLOPT_MAX_RECV_SPEED_LARGE, policy.recv_speed_limit);
	if (policy.low_speed_limit && policy.low_speed_time) {
		curl_easy_setopt(this->curlEasy, CURLOPT_LOW_SPEED_LIMIT, policy.low_speed_limit);
		curl_easy_setopt(this->curlEasy, CURLOPT_LOW_SPEED_TIME, policy.low_speed_time);
	}
	if (policy.curl_buffer_size) {
		unsigned long buf_size = std::clamp(policy.curl_buffer_size, 1024u, static_cast<unsigned int>(CURL_MAX_READ_SIZE));
		curl_easy_setopt(this->curlEasy, CURLOPT_BUFFERSIZE, buf_size);
	}

	for (const auto header : request.headers) {
		this->headers_ = curl_slist_append(this->headers_, header.c_str());
	}
	curl_easy_setopt(this->curlEasy, CURLOPT_HTTPHEADER, this->headers_);

	switch (HttpRequest::method2Enum(request.methodName)) {
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
			curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDS, request.body.c_str());
			curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDSIZE, request.body.size());
			break;
		}
		default: {
			curl_easy_setopt(this->curlEasy, CURLOPT_CUSTOMREQUEST, util::toupper(request.methodName).c_str());
			if (request.body.size()) {
				curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDS, request.body.c_str());
				curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDSIZE, request.body.size());
			}
		}
	}

	curl_easy_setopt(this->curlEasy, CURLOPT_WRITEFUNCTION, HttpTransfer::body_cb);
	curl_easy_setopt(this->curlEasy, CURLOPT_WRITEDATA, this);
	curl_easy_setopt(this->curlEasy, CURLOPT_HEADERFUNCTION, HttpTransfer::header_cb);
	curl_easy_setopt(this->curlEasy, CURLOPT_HEADERDATA, this);
};

HttpTransfer::~HttpTransfer() {
	curl_easy_cleanup(this->curlEasy);
	curl_slist_free_all(this->headers_);
}

HttpTransfer::HttpTransfer(HttpTransfer&& other) noexcept
	: curlEasy(std::exchange(other.curlEasy, nullptr)), headers_(std::exchange(other.headers_, nullptr)),
	  response(std::move(other.response)) {
	if (curlEasy) {
		curl_easy_setopt(curlEasy, CURLOPT_WRITEDATA, &response.body);
		curl_easy_setopt(curlEasy, CURLOPT_HEADERDATA, &response.headers);
	}
}

HttpTransfer& HttpTransfer::operator=(HttpTransfer&& other) noexcept {
	if (this != &other) {
		HttpTransfer tmp(std::move(other));
		std::swap(*this, tmp);
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

	curl_easy_getinfo(this->curlEasy, CURLINFO_QUEUE_TIME_T, &this->response.transferInfo.queue_s);
	curl_easy_getinfo(this->curlEasy, CURLINFO_CONNECT_TIME_T, &this->response.transferInfo.connect_s);
	curl_easy_getinfo(this->curlEasy, CURLINFO_APPCONNECT_TIME_T, &this->response.transferInfo.appconnect_s);
	curl_easy_getinfo(this->curlEasy, CURLINFO_PRETRANSFER_TIME_T, &this->response.transferInfo.pretransfer_s);
	curl_easy_getinfo(this->curlEasy, CURLINFO_POSTTRANSFER_TIME_T, &this->response.transferInfo.posttransfer_s);
	curl_easy_getinfo(this->curlEasy, CURLINFO_STARTTRANSFER_TIME_T, &this->response.transferInfo.starttransfer_s);
	curl_easy_getinfo(this->curlEasy, CURLINFO_TOTAL_TIME_T, &this->response.transferInfo.total_s);
	curl_easy_getinfo(this->curlEasy, CURLINFO_REDIRECT_TIME_T, &this->response.transferInfo.redir_s);

	this->response.transferInfo.receivetransfer_s = this->response.transferInfo.total_s - this->response.transferInfo.starttransfer_s;
	this->response.transferInfo.starttransfer_s -= this->response.transferInfo.posttransfer_s;
	this->response.transferInfo.posttransfer_s -= this->response.transferInfo.pretransfer_s;
	this->response.transferInfo.pretransfer_s -= this->response.transferInfo.appconnect_s;
	this->response.transferInfo.appconnect_s -= this->response.transferInfo.connect_s;
	this->response.transferInfo.connect_s -= this->response.transferInfo.queue_s;
}

void HttpTransfer::perform_blocking() {
	curl_easy_perform(this->curlEasy);
	this->finalize_transfer();
}

size_t HttpTransfer::body_cb(void* ptr, size_t size, size_t nmemb, void* data) {
	HttpTransfer* transfer = static_cast<HttpTransfer*>(data);
	if(transfer->response.transferInfo.ttfb == 0)
		transfer->response.transferInfo.ttfb = std::chrono::time_point_cast<std::chrono::microseconds>(
			std::chrono::system_clock::now()).time_since_epoch().count() - \
			transfer->response.transferInfo.start_at;
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

// BoundedSemaphore implementation
BoundedSemaphore::BoundedSemaphore(size_t initial_count, size_t max_count)
	: count_(initial_count), max_count_(max_count) {
	assert(initial_count <= max_count && "initial_count must lower than max_count");
}

void BoundedSemaphore::acquire() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [&]() { return count_ > 0; });
	--count_;
}

bool BoundedSemaphore::try_acquire() {
	std::unique_lock<std::mutex> lock(mutex_);
	if(count_ >= 1) {
		--count_;
		return true;
	}
	return false;
}

void BoundedSemaphore::release() {
	std::unique_lock<std::mutex> lock(mutex_);
	if (count_ < max_count_) {
		++count_;
	}
	cv_.notify_one();
}

// HttpClient::TransferState implementation
HttpClient::TransferState::TransferState(std::shared_future<HttpResponse>&& future, CURL* curl)
	: future(future), curl(curl) {}

void HttpClient::TransferState::cancel() {
	state.store(State::Cancel, std::memory_order_release);

	HttpClient& httpClient = HttpClient::getInstance();

	{
		std::unique_lock<std::mutex> lk(httpClient.mutex_);
		httpClient.toCancelled.emplace(this->curl);

		curl_multi_wakeup(httpClient.multi_);
	}
}

void HttpClient::TransferState::pause() {
	State expected = State::Ongoing;
	if (!state.compare_exchange_strong(expected, State::Pause, std::memory_order_acq_rel)) {
		// If not from Ongoing, discard
		return;
	}

	HttpClient& httpClient = HttpClient::getInstance();

	{
		std::unique_lock<std::mutex> lk(httpClient.mutex_);
		httpClient.toPaused.emplace(this->curl);

		curl_multi_wakeup(httpClient.multi_);
	}
}

void HttpClient::TransferState::resume() {
	State expected = State::Paused;
	if (!state.compare_exchange_strong(expected, State::Ongoing, std::memory_order_acq_rel)) {
		// If not from Paused, discard
		return;
	}

	HttpClient& httpClient = HttpClient::getInstance();

	{
		std::unique_lock<std::mutex> lk(httpClient.mutex_);
		httpClient.toResumed.emplace(this->curl);

		curl_multi_wakeup(httpClient.multi_);
	}
}

HttpClient::TransferState::State HttpClient::TransferState::get_state() {
	return this->state.load(std::memory_order_acquire);
}

// HttpClient::TransferTask implementation
HttpClient::TransferTask::TransferTask(HttpRequest r, RequestPolicy p)
	: transfer(r, p), state(std::shared_ptr<TransferState>(
					   new TransferState{this->promise.get_future().share(), this->transfer.curlEasy})) {};

// HttpClient implementation
HttpClient& HttpClient::getInstance() {
	static HttpClient instance;
	return instance;
}

void HttpClient::stop() {
	if (!this->stop_.load()) {
		this->stop_.store(true);
		curl_multi_wakeup(this->multi_);
	}
}

HttpClient::HttpClient()
	: sema_(MAX_CONNECTION, MAX_CONNECTION), uplinkAvgSpeed(SPEED_TRACK_WINDOW), downlinkAvgSpeed(SPEED_TRACK_WINDOW) {
	curl_global_init(CURL_GLOBAL_DEFAULT);

	this->multi_ = curl_multi_init();
	CURL_MULTI_DEFAULT_SETTING(this->multi_);
	this->worker_ = std::thread(&HttpClient::worker_loop, this);
}

HttpClient::~HttpClient() {
	this->stop();
	if (this->worker_.joinable())
		this->worker_.join();

	curl_multi_cleanup(this->multi_);
	curl_global_cleanup();
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

					it->transfer.finalize_transfer();

					// Record uplink and downlink speed
					curl_off_t upSpeed;
					curl_off_t dlSpeed;
					curl_easy_getinfo(easy, CURLINFO_SPEED_UPLOAD_T, &upSpeed);
					curl_easy_getinfo(easy, CURLINFO_SPEED_DOWNLOAD_T, &dlSpeed);
					this->downlinkAvgSpeed.push(dlSpeed);
					this->uplinkAvgSpeed.push(upSpeed);

					// Put response into promise
					it->promise.set_value(std::move(it->transfer.detachResponse()));

					// Tag it completed
					it->state->state.store(TransferState::State::Completed, std::memory_order_release);
					this->curl2Task.erase(easy);
					this->transfers.erase(it);
				} else {
#ifndef DNDEBUG
					throw std::runtime_error("Dangling CURL pointer detected!");
#endif
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

		// Handle cancel
		std::vector<CURL*> cancels;
		{
			std::lock_guard<std::mutex> lk(this->mutex_);
			while (!this->toCancelled.empty()) {
				cancels.push_back(this->toCancelled.front());
				this->toCancelled.pop();
			}
		}

		for (CURL* curlEasy : cancels) {
			if (!curlEasy)
				continue;

			auto mit = this->curl2Task.find(curlEasy);
			if (mit == this->curl2Task.end() || !mit->second)
				continue;

			auto it = mit->second.value();

			curl_multi_remove_handle(this->multi_, curlEasy);
			this->sema_.release();
			it->promise.set_exception(std::make_exception_ptr(std::runtime_error("The task is cancelled.")));

			this->curl2Task.erase(curlEasy);
			this->transfers.erase(it);
		}

		// Handle pause
		std::vector<CURL*> pauses;
		{
			std::lock_guard<std::mutex> lk(this->mutex_);
			while (!this->toPaused.empty()) {
				pauses.push_back(this->toPaused.front());
				this->toPaused.pop();
			}
		}

		for (CURL* curlEasy : pauses) {
			if (!curlEasy)
				continue;

			auto mit = this->curl2Task.find(curlEasy);
			if (mit == this->curl2Task.end() || !mit->second)
				continue;

			auto it = mit->second.value();
			curl_easy_pause(curlEasy, CURLPAUSE_ALL);
			it->state->state.store(TransferState::Paused, std::memory_order_release);
			this->sema_.release();
		}

		// Handle resumes
		std::vector<CURL*> resumes;
		{
			std::lock_guard<std::mutex> lk(this->mutex_);
			while (!this->toResumed.empty()) {
				// Acquire semaphore before pop, leave remaining for next epoch
				if (!this->sema_.try_acquire())
					break;
				resumes.push_back(this->toResumed.front());
				this->toResumed.pop();
			}
		}

		for (CURL* curlEasy : resumes) {
			if (!curlEasy) {
				this->sema_.release();
				continue;
			}

			auto mit = this->curl2Task.find(curlEasy);
			if (mit == this->curl2Task.end() || !mit->second) {
				this->sema_.release();
				continue;
			}

			auto it = mit->second.value();
			curl_easy_pause(curlEasy, CURLPAUSE_CONT);
		}

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

HttpResponse HttpClient::request(HttpRequest request, RequestPolicy policy) {
	std::shared_ptr<TransferState> state = this->send_request(std::move(request), std::move(policy));

	return state->future.get();
}

std::shared_ptr<HttpClient::TransferState> HttpClient::send_request(HttpRequest request, RequestPolicy policy) {
	TransferTask task(std::move(request), std::move(policy));
	std::shared_ptr<TransferState> state = task.state;

	this->sema_.acquire();
	std::this_thread::sleep_for(std::chrono::duration<float, std::milli>(std::abs(jitter_generator(10))));

	{
		std::unique_lock lk(this->mutex_);
		this->requests.emplace(std::move(task));
	}
	curl_multi_wakeup(this->multi_);

	return state;
}

float jitter_generator(float max) {
    max = std::max(0.0f, max);
    if (max == 0.0) return 0.0;

    thread_local std::mt19937_64 rg{
        [] {
            std::random_device rd;
            std::seed_seq seq{
                rd(), rd(), rd(), rd(),
                static_cast<unsigned>(
                    std::hash<std::thread::id>{}(std::this_thread::get_id()))
            };
            return std::mt19937_64(seq);
        }()
    };

    // ---- sigma scaling with max ----
    const float ref       = 1e-3;  // 1ms
    const float sigma_min = 0.3;
    const float sigma_max = 1.5;

    float sigma = std::clamp(
        0.4f + 0.3f * std::log1p(max / ref),
        sigma_min,
        sigma_max
    );

    // median ≈ 5% of max
    float mu = std::log(0.05 * max + 1e-12);

    std::lognormal_distribution<float> mag_dist(mu, sigma);
    std::bernoulli_distribution sign_dist(0.5);

    float mag = mag_dist(rg);
    if (mag > max) mag = max;

    return sign_dist(rg) ? mag : -mag;
}

} // namespace http_client
