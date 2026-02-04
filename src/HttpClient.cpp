#include "HttpClient.hpp"

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
	curl_multi_setopt(handle, CURLMOPT_MAX_HOST_CONNECTIONS, 2L);
	curl_multi_setopt(handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, 4L);
	curl_multi_setopt(handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
	curl_multi_setopt(handle, CURLMOPT_MAXCONNECTS, 8L);
}

// HttpTransfer implementation
HttpTransfer::HttpTransfer(const HttpRequest& request) : url(request.url) {
	this->curlEasy = curl_easy_init();

	CURL_EASY_DEFAULT_SETTING(this->curlEasy);

	curl_easy_setopt(this->curlEasy, CURLOPT_URL, request.url.c_str());
	if (request.timeout_ms > 0)
		curl_easy_setopt(this->curlEasy, CURLOPT_TIMEOUT_MS, request.timeout_ms);
	if (request.conn_timeout_ms > 0)
		curl_easy_setopt(this->curlEasy, CURLOPT_CONNECTTIMEOUT_MS, request.conn_timeout_ms);

	for (const auto header : request.headers) {
		this->headers_ = curl_slist_append(this->headers_, header.c_str());
	}
	curl_easy_setopt(this->curlEasy, CURLOPT_HTTPHEADER, this->headers_);

	switch (request.method) {
	case HttpRequest::HEAD: {
		curl_easy_setopt(this->curlEasy, CURLOPT_NOBODY, 1L);
		break;
	}
	case HttpRequest::GET: {
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
		curl_easy_setopt(this->curlEasy, CURLOPT_CUSTOMREQUEST, request.methodName.c_str());
		if (request.body.size()) {
			curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDS, request.body.c_str());
			curl_easy_setopt(this->curlEasy, CURLOPT_POSTFIELDSIZE, request.body.size());
		}
	}
	}

	curl_easy_setopt(this->curlEasy, CURLOPT_WRITEFUNCTION, this->body_cb);
	curl_easy_setopt(this->curlEasy, CURLOPT_WRITEDATA, &response.body);
	curl_easy_setopt(this->curlEasy, CURLOPT_HEADERFUNCTION, this->header_cb);
	curl_easy_setopt(this->curlEasy, CURLOPT_HEADERDATA, &response.headers);
}

HttpTransfer::~HttpTransfer() {
	curl_easy_cleanup(this->curlEasy);
	curl_slist_free_all(this->headers_);
}

HttpTransfer::HttpTransfer(HttpTransfer&& other) noexcept : curlEasy(std::exchange(other.curlEasy, nullptr)),
															headers_(std::exchange(other.headers_, nullptr)),
															response(std::move(other.response)),
															url(std::move(other.url)) {
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
	curl_easy_getinfo(this->curlEasy, CURLINFO_TOTAL_TIME, &this->response.elapsed);
}

void HttpTransfer::perform_blocking() {
	curl_easy_perform(this->curlEasy);
	this->finalize_transfer();
}

size_t HttpTransfer::body_cb(void* ptr, size_t size, size_t nmemb, std::string* data) {
	data->append((char*)ptr, size * nmemb);
	return size * nmemb;
}

size_t HttpTransfer::header_cb(void* ptr, size_t size, size_t nmemb, std::vector<std::string>* data) {
	const size_t len = size * nmemb;
	if (!ptr || len == 0) return len;

	std::string_view sv(static_cast<const char*>(ptr), len);

	if (!sv.empty() && sv.back() == '\n') sv.remove_suffix(1);
	if (!sv.empty() && sv.back() == '\r') sv.remove_suffix(1);

	if (sv.empty()) return len;
	if (sv.rfind("HTTP/", 0) == 0) return len;

	data->emplace_back(sv);

	return len;
}

// BoundedSemaphore implementation
BoundedSemaphore::BoundedSemaphore(size_t initial_count, size_t max_count) : count_(initial_count), max_count_(max_count) {
	assert(initial_count <= max_count && "initial_count must lower than max_count");
}

void BoundedSemaphore::acquire() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [&]() { return count_ > 0; });
	--count_;
}

void BoundedSemaphore::release() {
	std::unique_lock<std::mutex> lock(mutex_);
	if (count_ < max_count_) {
		++count_;
	}
	cv_.notify_one();
}

// HttpClient::TransferState implementation
HttpClient::TransferState::TransferState(std::shared_future<HttpResponse>&& future, CURL* curl) :
	future(future), curl(curl) {}

void HttpClient::TransferState::cancel() {
	State expected = State::Ongoing;
	if (!state.compare_exchange_strong(expected, State::Cancel,
									std::memory_order_acq_rel)) {
		return;
	}

	HttpClient& httpClient = HttpClient::getInstance();

	{
		std::unique_lock<std::mutex> lk(httpClient.mutex_);
		httpClient.toCancelled.emplace(this->curl);

		curl_multi_wakeup(httpClient.multi_);
	}
}

HttpClient::TransferState::State HttpClient::TransferState::get_state() {
	return this->state.load(std::memory_order_acquire);
}

// HttpClient::TransferTask implementation
HttpClient::TransferTask::TransferTask(HttpRequest r) :
	transfer(r),
	state(std::shared_ptr<TransferState>(new TransferState{this->promise.get_future().share(), this->transfer.curlEasy})) {};

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

HttpClient::HttpClient() : sema_(MAX_CONNECTION, MAX_CONNECTION) {
	curl_global_init(CURL_GLOBAL_DEFAULT);

	this->multi_ = curl_multi_init();
	CURL_MULTI_DEFAULT_SETTING(this->multi_);
	this->worker_ = std::thread(&HttpClient::worker_loop, this);
}

HttpClient::~HttpClient() {
	this->stop();
	if (this->worker_.joinable()) this->worker_.join();

	curl_multi_cleanup(this->multi_);
	curl_global_cleanup();
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
				if(mit != this->curl2Task.end() && mit->second) {
					auto it = mit->second.value();

					it->transfer.finalize_transfer();
					it->state->state.store(TransferState::State::Completed, std::memory_order_release);
					it->promise.set_value(std::move(it->transfer.detachResponse()));

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
		if (t < 0) poll_timeout = POLL_MS;
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
				it->promise.set_exception(std::make_exception_ptr(std::runtime_error("The HttpClient stopped while task in the pool.")));
			}

			transfers.clear();
			// Exit the worker loop
			break;
		}

		// Handle cancel
		{				
			std::vector<CURL*> cancels;
			{
				std::lock_guard<std::mutex> lk(this->mutex_);
				while (!this->toCancelled.empty()) {
					cancels.push_back(this->toCancelled.front());
					this->toCancelled.pop();
				}
			}

			for (CURL* curlEasy : cancels) {
				if(!curlEasy) continue;

				auto mit = this->curl2Task.find(curlEasy);
				if (mit == this->curl2Task.end() || !mit->second) continue;

				auto it = mit->second.value();

				curl_multi_remove_handle(this->multi_, curlEasy);
				this->sema_.release();
				it->promise.set_exception(std::make_exception_ptr(std::runtime_error("The task is cancelled.")));

				this->curl2Task.erase(curlEasy);
				this->transfers.erase(it);
			}
		}

		// Add new request
		std::vector<TransferTask> reqs;
		{
			std::unique_lock<std::mutex> lk(this->mutex_);

			reqs.reserve(this->requests.size());
			while(!this->requests.empty()) {
				reqs.emplace_back(std::move(this->requests.front()));
				this->requests.pop();
			}
		}

		for (auto &&task : reqs) {
			this->transfers.emplace_back(std::move(task));
			auto it = std::prev(this->transfers.end());
			this->curl2Task[this->transfers.back().transfer.curlEasy] = it;

			curl_multi_add_handle(this->multi_, this->transfers.back().transfer.curlEasy);
		}
	}
}

}// namespace http_client
