#pragma once

#include "models.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace http_client {

/**
 * Record of a single HTTP request attempt.
 * Stores the response, CURL error code, and completion timestamp.
 */
struct AttemptRecord {
    HttpResponse response;                          // Response from this attempt
    CURLcode curlCode = CURLE_OK;                   // CURL error code, CURLE_OK if HTTP response received
    double complete_at = 0;                         // When this attempt completed, in seconds since epoch
};

/**
 * Context passed to retry condition and backoff functions.
 * Contains all information needed for decision making.
 */
struct RetryContext {
    std::vector<AttemptRecord> attempts;            // History of all attempts, last element is most recent
    double first_attempt_at = 0;                    // When first attempt started, in seconds since epoch

    // Convenience accessors
    uint32_t attemptCount() const { return static_cast<uint32_t>(attempts.size()); }
    const AttemptRecord* lastAttempt() const { return attempts.empty() ? nullptr : &attempts.back(); }
    double lastCompleteAt() const { return attempts.empty() ? 0 : attempts.back().complete_at; }
};

/**
 * Type alias for retry condition function.
 * Returns true if the request should be retried based on the context.
 */
using RetryConditionFn = std::function<bool(const RetryContext&)>;

/**
 * Type alias for backoff scheduling function.
 * Returns the absolute timestamp in seconds when to retry next.
 * Based on ctx.lastCompleteAt() from the last response.
 */
using BackoffScheduleFn = std::function<double(const RetryContext&)>;

/**
 * Configuration for retry behavior.
 * Contains limits and pluggable condition/backoff functions.
 */
struct RetryPolicy {
    uint32_t maxRetries = 3;                        // Maximum retry attempts (not including initial)
    float totalTimeout = 0;                         // Total timeout in seconds from first attempt, 0 = no limit

    RetryConditionFn shouldRetry;                   // Retry condition function
    BackoffScheduleFn getNextRetryTime;             // Returns absolute time in seconds for next retry

    // Default constructor - uses default condition and exponential backoff
    // Defined in RetryStrategies.hpp after factory functions are available
    RetryPolicy();
};

} // namespace http_client
