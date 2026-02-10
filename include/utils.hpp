#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace http_client {
namespace util {

inline std::string toupper(const std::string& str) {
    std::string s(str);
    for (char& c : s)
        if (c >= 'a' && c <= 'z')
            c -= 32;
    return s;
}

/**
 * Generate jitter value for backoff delays.
 * Returns a value in range [-max, max] with log-normal distribution.
 */
inline float jitter_generator(float max) {
    max = std::max(0.0f, max);
    if (max == 0.0f) return 0.0f;

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
    const float ref       = 1e-3f;  // 1ms
    const float sigma_min = 0.3f;
    const float sigma_max = 1.5f;

    float sigma = std::clamp(
        0.4f + 0.3f * std::log1p(max / ref),
        sigma_min,
        sigma_max
    );

    // median ≈ 5% of max
    float mu = std::log(0.05f * max + 1e-12f);

    std::lognormal_distribution<float> mag_dist(mu, sigma);
    std::bernoulli_distribution sign_dist(0.5);

    float mag = mag_dist(rg);
    if (mag > max) mag = max;

    return sign_dist(rg) ? mag : -mag;
}

} // namespace util

template <typename T, class = std::enable_if_t<std::is_arithmetic<T>::value>>
class SlidingWindow {
public:
    explicit SlidingWindow(size_t capacity) : cap(capacity) {
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

    T mean() const {
        return this->size ? static_cast<double>(sum_) / this->size : 0.0;
    }

    T max() const {
        auto it = std::max_element(this->buffer.begin(), this->buffer.end());
        return it == this->buffer.end() ? 0 : *it;
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

class BoundedSemaphore {
public:
    explicit BoundedSemaphore(size_t initial_count, size_t max_count)
        : count_(initial_count), max_count_(max_count) {
        assert(initial_count <= max_count && "initial_count must lower than max_count");
    }

    // non-copyable
    BoundedSemaphore(const BoundedSemaphore&) = delete;
    BoundedSemaphore& operator=(const BoundedSemaphore&) = delete;

    // Acquire the semaphore (P operation, blocking)
    void acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&]() { return count_ > 0; });
        --count_;
    }

    // Acquire the semaphore (Non-blocking)
    bool try_acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (count_ >= 1) {
            --count_;
            return true;
        }
        return false;
    }

    // Release the semaphore (V operation)
    void release() {
        std::unique_lock<std::mutex> lock(mutex_);
        if (count_ < max_count_) {
            ++count_;
        }
        cv_.notify_one();
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    unsigned int count_;
    const unsigned int max_count_;
};

} // namespace http_client
