#include "HttpClient.hpp"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

void printResponse(const http_client::HttpResponse &response) {
  std::cout << "Elapsed: " << response.elapsed << "s" << std::endl;
  std::cout << "Status: " << response.status << std::endl;
  std::cout << "Headers:" << std::endl;
  for (const auto &h : response.headers) {
    std::cout << "  " << h << std::endl;
  }
  std::cout << "Body: " << std::endl << response.body << std::endl;
};

void testGET() {
  std::cout << "GET request..." << std::endl;

  // Create GET request to reqbin.com echo endpoint
  http_client::HttpRequest request("https://httpbin.org/get", "GET",
                                   5000 // 5 second timeout
  );

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
  http_client::HttpRequest request(
      "https://httpbin.org/post", "POST",
      5000,                                  // 5 second timeout
      0, {"Content-Type: application/json"}, // JSON content type header
      jsonBody);

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
  auto &client = http_client::HttpClient::getInstance();

  std::cout << "\nLaunching " << numRequests
            << " concurrent requests with different data..." << std::endl;

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

        responses[i] = client.request(
            "https://httpbin.org/get?thread=" + std::to_string(i), "GET",
            10000 // 10 second timeout
        );

        {
          std::lock_guard<std::mutex> lock(cout_mutex);
          std::cout << "[Thread " << i << "] Response received:" << std::endl;
          std::cout << "  Status: " << responses[i].status << std::endl;
          std::cout << "  Elapsed: " << responses[i].elapsed << "s"
                    << std::endl;
          std::cout << "  Error: " << responses[i].error << std::endl;
          std::cout << "  Body length: " << responses[i].body.length()
                    << std::endl;

          // Display the echoed data from response
          std::cout << "  Body:" << std::endl << responses[i].body << std::endl;
          std::cout << std::endl;
        }

      } catch (const std::exception &e) {
        std::lock_guard<std::mutex> lock(cout_mutex);
        std::cerr << "[Thread " << i << "] Exception: " << e.what()
                  << std::endl;
      }
    });
  }

  // Wait for all threads to complete
  for (auto &thread : threads) {
    thread.join();
  }

  // Record end time
  auto endTime = std::chrono::high_resolution_clock::now();
  auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(
      endTime - startTime);

  std::cout << "\n----------------------------------------" << std::endl;
  std::cout << "Results Summary:" << std::endl;
  std::cout << "----------------------------------------" << std::endl;
  std::cout << "Total requests: " << numRequests << std::endl;
  std::cout << "Total wall-clock time: " << totalDuration.count() / 1000.0
            << "s" << std::endl;
  std::cout << "----------------------------------------" << std::endl;
}

void testCancel() {
  auto &client = http_client::HttpClient::getInstance();

  std::cout << "Cancel request through connection pool..." << std::endl;

  auto transferState =
      client.send_request("https://httpbin.org/delay/10", "GET");

  auto now1 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::cout << "[" << std::put_time(std::localtime(&now1), "%Y-%m-%d %H:%M:%S")
            << "] " << "Wait for 3 seconds." << std::endl;
  std::this_thread::sleep_for(std::chrono::seconds(3));
  transferState->cancel();
  auto now2 =
      std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  std::cout << "[" << std::put_time(std::localtime(&now2), "%Y-%m-%d %H:%M:%S")
            << "] " << "Transfer cancelled." << std::endl;
}

int main() {
  std::cout << "========================================" << std::endl;
  std::cout << "   HttpClient.hpp Example" << std::endl;
  std::cout << "========================================" << std::endl
            << std::endl;

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

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Failed with exception: " << e.what() << std::endl;
    return 1;
  }
}
