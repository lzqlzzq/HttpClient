#include "httpclient/HttpClient.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>

using namespace http_client;
using namespace std::chrono_literals;

// Hold the worker before any socket is opened: no server or network is needed.
struct Settings : HttpClientSettings {
    mutable std::promise<void> entered;
    std::promise<void> release;
    std::shared_future<void> gate = release.get_future().share();
    mutable std::once_flag once;

    static curl_socket_t open(void* context, curlsocktype, curl_sockaddr*) {
        auto& self = *static_cast<const Settings*>(context);
        std::call_once(self.once, [&] { self.entered.set_value(); });
        self.gate.wait();
        return CURL_SOCKET_BAD;
    }

    void applyCurlEasySettings(CURL* handle) const override {
        HttpClientSettings::applyCurlEasySettings(handle);
        curl_easy_setopt(handle, CURLOPT_PROXY, "");
        curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, &Settings::open);
        curl_easy_setopt(handle, CURLOPT_OPENSOCKETDATA, this);
    }
};

static void checkAdmission(long cacheSize, unsigned int concurrency) {
    Settings settings;
    settings.maxConnections = cacheSize;
    settings.maxConcurrentRequests = concurrency;
    settings.maxTotalConnections = 0; // Unlimited connections must not disable admission.
    HttpClient client(settings);
    HttpRequest request;
    request.url = "http://127.0.0.1:1/";
    auto entered = settings.entered.get_future();
    auto first = client.send_request(request);
    if (entered.wait_for(2s) != std::future_status::ready) {
        settings.release.set_value();
        throw std::runtime_error("worker did not reach socket callback");
    }
    auto second = std::async(std::launch::async, [&] { return client.send_request(request); });
    const bool admitted = second.wait_for(500ms) == std::future_status::ready;
    settings.release.set_value();
    (void)second.get()->future.get();
    (void)first->future.get();
    (void)first->get_state(); // Also exercise the exported nested class from shared builds.
    if (admitted != (concurrency > 1))
        throw std::runtime_error("request admission depends on connection cache size");
}

int main() {
    try {
        checkAdmission(1, 2);
        checkAdmission(24, 1);
        HttpClientSettings settings;
        settings.maxConcurrentRequests = 0;
        try {
            HttpClient client(settings);
            throw std::runtime_error("zero request limit was accepted");
        } catch (const std::invalid_argument&) {}
        HttpClient defaultClient;
        HttpClient explicitDefault(HttpClientSettings::getDefault());
        if (defaultClient.settings().maxConcurrentRequests != explicitDefault.settings().maxConcurrentRequests)
            throw std::runtime_error("constructor defaults differ");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
