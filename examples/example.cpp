#include "HttpClient.hpp"
#include "RetryStrategies.hpp"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

void printResponse(const http_client::HttpResponse& response) {
	std::cout << "Elapsed: " << response.transferInfo.total << "s" << std::endl;
	std::cout << "Status: " << response.status << std::endl;
	std::cout << "Headers:" << std::endl;
	for (const auto& h : response.headers) {
		std::cout << "  " << h << std::endl;
	}
	std::cout << "Body: " << std::endl << response.body << std::endl;
};

void testGET() {
	std::cout << "GET request..." << std::endl;

	// Create GET request to httpbin.org echo endpoint
	http_client::HttpRequest request;
	request.url = "https://httpbin.org/get";
	request.methodName = "GET";

	// Create HttpTransfer and perform blocking request
	http_client::HttpTransfer transfer(request);
	transfer.perform_blocking();

	printResponse(transfer.getResponse());
}

void testPOST() {
	std::cout << "POST request..." << std::endl;

	// JSON body to send
	std::string jsonBody = R"({"name":"test","value":"123"})";

	// Create POST request with JSON content
	http_client::HttpRequest request;
	request.url = "https://httpbin.org/post";
	request.methodName = "POST";
	request.headers = {"Content-Type: application/json"};
	request.body = jsonBody;

	// Create HttpTransfer and perform blocking request
	http_client::HttpTransfer transfer(request);
	transfer.perform_blocking();

	printResponse(transfer.getResponse());
}

// Global mutex for protecting cout in multi-threaded tests
std::mutex cout_mutex;

void testAsyncConcurrent() {
	std::cout << "\n========================================" << std::endl;
	std::cout << "Async Concurrent POST Requests..." << std::endl;
	std::cout << "========================================" << std::endl;

	const int numRequests = 5;

	// Get the HttpClient singleton instance
	auto& client = http_client::HttpClient::getInstance();

	std::cout << "\nLaunching " << numRequests << " concurrent requests with different data..." << std::endl;

	// Record start time
	auto startTime = std::chrono::high_resolution_clock::now();

	// Vector to hold threads
	std::vector<std::thread> threads;
	std::vector<http_client::HttpResponse> responses(numRequests);
	std::vector<std::string> requestBodies(numRequests);

	// Launch multiple threads, each making a different POST request
	for (int i = 0; i < numRequests; ++i) {
		threads.emplace_back([&client, &responses, &requestBodies, i]() {
			try {
				{
					std::lock_guard<std::mutex> lock(cout_mutex);
					std::cout << "\n[Thread " << i << "] Sending request." << std::endl;
				}

				http_client::HttpRequest request;
				request.url = "https://httpbin.org/get?thread=" + std::to_string(i);
				request.methodName = "GET";

				responses[i] = client.request(request);

				{
					std::lock_guard<std::mutex> lock(cout_mutex);
					std::cout << "[Thread " << i << "] Response received:" << std::endl;
					std::cout << "  Status: " << responses[i].status << std::endl;
					std::cout << "  Elapsed: " << responses[i].transferInfo.total << "s" << std::endl;
					std::cout << "  Error: " << responses[i].error << std::endl;
					std::cout << "  Body length: " << responses[i].body.length() << std::endl;

					// Display the echoed data from response
					std::cout << "  Body:" << std::endl << responses[i].body << std::endl;
					std::cout << std::endl;
				}

			} catch (const std::exception& e) {
				std::lock_guard<std::mutex> lock(cout_mutex);
				std::cerr << "[Thread " << i << "] Exception: " << e.what() << std::endl;
			}
		});
	}

	// Wait for all threads to complete
	for (auto& thread : threads) {
		thread.join();
	}

	// Record end time
	auto endTime = std::chrono::high_resolution_clock::now();
	auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

	std::cout << "\n----------------------------------------" << std::endl;
	std::cout << "Results Summary:" << std::endl;
	std::cout << "----------------------------------------" << std::endl;
	std::cout << "Total requests: " << numRequests << std::endl;
	std::cout << "Total wall-clock time: " << totalDuration.count() / 1000.0 << "s" << std::endl;
	std::cout << "----------------------------------------" << std::endl;
}

void testCancel() {
	auto& client = http_client::HttpClient::getInstance();

	std::cout << "Cancel request through connection pool..." << std::endl;

	http_client::HttpRequest request;
	request.url = "https://httpbin.org/delay/10";
	request.methodName = "GET";

	auto transferState = client.send_request(request);

	auto now1 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::cout << "[" << std::put_time(std::localtime(&now1), "%Y-%m-%d %H:%M:%S") << "] " << "Wait for 3 seconds."
			  << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(3));
	transferState->cancel();
	auto now2 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::cout << "[" << std::put_time(std::localtime(&now2), "%Y-%m-%d %H:%M:%S") << "] " << "Transfer cancelled."
			  << std::endl;
}

void testPauseResume() {
	auto& client = http_client::HttpClient::getInstance();

	std::cout << "Pause and Resume request test..." << std::endl;

	// Use a large file download to test pause/resume
	http_client::HttpRequest request;
	request.url = "https://httpbin.org/drip?duration=10&numbytes=1000&code=200&delay=0";
	request.methodName = "GET";

	auto transferState = client.send_request(request);

	auto printTime = []() {
		auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::cout << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] ";
	};

	// Wait for transfer to start
	std::this_thread::sleep_for(std::chrono::seconds(1));
	printTime();
	std::cout << "State: " << static_cast<int>(transferState->get_state()) << " (Ongoing=1)" << std::endl;

	// Pause the transfer
	printTime();
	std::cout << "Pausing transfer..." << std::endl;
	transferState->pause();

	// Wait a bit for pause to take effect
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	printTime();
	std::cout << "State after pause: " << static_cast<int>(transferState->get_state()) << " (Paused=4)" << std::endl;

	// Wait while paused
	printTime();
	std::cout << "Waiting 3 seconds while paused..." << std::endl;
	std::this_thread::sleep_for(std::chrono::seconds(3));

	// Resume the transfer
	printTime();
	std::cout << "Resuming transfer..." << std::endl;
	transferState->resume();

	// Wait a bit for resume to take effect
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	printTime();
	std::cout << "State after resume: " << static_cast<int>(transferState->get_state()) << " (Ongoing=1)" << std::endl;

	// Wait for completion
	try {
		auto response = transferState->future.get();
		printTime();
		std::cout << "Transfer completed!" << std::endl;
		printResponse(response);
	} catch (const std::exception& e) {
		printTime();
		std::cerr << "Transfer failed: " << e.what() << std::endl;
	}
}

void testRetry() {
	std::cout << "\n========================================" << std::endl;
	std::cout << "Retry Request Test..." << std::endl;
	std::cout << "========================================" << std::endl;

	auto& client = http_client::HttpClient::getInstance();

	auto printTime = []() {
		auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::cout << "[" << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << "] ";
	};

	// Create request to an endpoint that returns 500 error (will trigger retry)
	http_client::HttpRequest request;
	request.url = "https://httpbin.org/status/503";  // Returns 503 Service Unavailable
	request.methodName = "GET";

	// Configure retry policy
	http_client::RetryPolicy retryPolicy;
	retryPolicy.maxRetries = 3;
	retryPolicy.totalTimeout = 30.0f;  // 30 seconds total timeout

	// Retry on HTTP 5xx errors with exponential backoff
	retryPolicy.shouldRetry = http_client::retry::httpStatusCondition({500, 502, 503, 504});
	retryPolicy.getNextRetryTime = http_client::retry::exponentialBackoff(
		1.0f,   // baseDelay: 1s
		10.0f,  // maxDelay: 10s
		2.0f,   // multiplier
		0.2f    // jitterFactor
	);

	printTime();
	std::cout << "Sending request with retry (expecting 503 response)..." << std::endl;
	std::cout << "Max retries: " << retryPolicy.maxRetries << std::endl;

	auto startTime = std::chrono::high_resolution_clock::now();

	// Use async interface to monitor retry progress
	auto transferState = client.send_request(request, http_client::RequestPolicy(), retryPolicy);

	// Monitor retry attempts
	while (transferState->get_state() != http_client::HttpClient::TransferState::Completed &&
	       transferState->get_state() != http_client::HttpClient::TransferState::Failed) {
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		if (transferState->hasRetry()) {
			auto ctx = transferState->getRetryContext();
			if (ctx) {
				static uint32_t lastAttempt = 0;
				uint32_t currentAttempt = ctx->get().attemptCount();
				if (currentAttempt > lastAttempt) {
					printTime();
					std::cout << "Attempt " << currentAttempt << " completed";
					if (!ctx->get().attempts.empty()) {
						auto& last = ctx->get().attempts.back();
						std::cout << " - Status: " << last.response.status
						          << ", CURL code: " << last.curlCode;
					}
					std::cout << std::endl;
					lastAttempt = currentAttempt;
				}
			}
		}
	}

	auto endTime = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

	try {
		auto response = transferState->future.get();
		printTime();
		std::cout << "Request completed after " << duration.count() / 1000.0 << "s" << std::endl;
		std::cout << "Final status: " << response.status << std::endl;
		std::cout << "Total attempts: " << transferState->getAttempt() << std::endl;
	} catch (const std::exception& e) {
		printTime();
		std::cerr << "Request failed: " << e.what() << std::endl;
	}

	std::cout << "\n--- Testing successful request with retry policy ---" << std::endl;

	// Test with a successful endpoint (should not retry)
	http_client::HttpRequest successRequest;
	successRequest.url = "https://httpbin.org/get";
	successRequest.methodName = "GET";

	printTime();
	std::cout << "Sending request (expecting 200 response, no retry needed)..." << std::endl;

	auto response = client.request(successRequest, http_client::RequestPolicy(), retryPolicy);

	printTime();
	std::cout << "Response status: " << response.status << std::endl;
	std::cout << "Body length: " << response.body.length() << " bytes" << std::endl;
}

int main() {
	std::cout << "========================================" << std::endl;
	std::cout << "   HttpClient.hpp Example" << std::endl;
	std::cout << "========================================" << std::endl << std::endl;

	std::cout << "Note: These exmples require internet connection" << std::endl;
	std::cout << "      to reach https://httpbin.org/" << std::endl << std::endl;

	try {
		// Test 1: Basic blocking GET
		std::cout << "\n[1] Basic GET (blocking mode)" << std::endl;
		std::cout << "----------------------------------------" << std::endl;
		curl_global_init(CURL_GLOBAL_DEFAULT);
		testGET();
		curl_global_cleanup();

		// Test 2: Basic blocking POST
		std::cout << "\n[2] Basic POST (blocking mode)" << std::endl;
		std::cout << "----------------------------------------" << std::endl;
		curl_global_init(CURL_GLOBAL_DEFAULT);
		testPOST();
		curl_global_cleanup();

		// Test 3: Async concurrent requests (using HttpClient singleton)
		std::cout << "\n[3] Async Concurrent Requests" << std::endl;
		testAsyncConcurrent();

		// Test 4: Test cancel
		std::cout << "\n[4] Cancel Request" << std::endl;
		testCancel();

		// Test 5: Test pause and resume
		std::cout << "\n[5] Pause and Resume Request" << std::endl;
		std::cout << "----------------------------------------" << std::endl;
		testPauseResume();

		// Test 6: Test retry
		std::cout << "\n[6] Retry Request" << std::endl;
		std::cout << "----------------------------------------" << std::endl;
		testRetry();

		auto& client = http_client::HttpClient::getInstance();

		std::cout << "\nAvarage download speed (bytes/s): " << std::to_string(client.downlinkSpeed());
		std::cout << "\nAvarage upload speed (bytes/s): " << std::to_string(client.uplinkSpeed()) << std::endl;

		return 0;
	} catch (const std::exception& e) {
		std::cerr << "Failed with exception: " << e.what() << std::endl;
		return 1;
	}
}
