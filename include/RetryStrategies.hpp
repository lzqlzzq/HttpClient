#pragma once

#include "RetryPolicy.hpp"
#include "utils.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

namespace http_client {
namespace retry {

// ============ Retry Conditions ============

/**
 * Default retry condition: retry on transient CURL errors.
 */
inline RetryConditionFn defaultCondition() {
    return [](const RetryContext& ctx) -> bool {
        auto* last = ctx.lastAttempt();
        if (!last) return false;

        switch (last->curlCode) {
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_CONNECT:
            case CURLE_OPERATION_TIMEDOUT:
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SEND_ERROR:
            case CURLE_RECV_ERROR:
            case CURLE_GOT_NOTHING:
                return true;
            default:
                return false;
        }
    };
}

/**
 * Retry on specific HTTP status codes.
 * Default: 429 (Too Many Requests), 500, 502, 503, 504 (Server Errors)
 */
inline RetryConditionFn httpStatusCondition(std::set<int> codes = {429, 500, 502, 503, 504}) {
    return [codes = std::move(codes)](const RetryContext& ctx) -> bool {
        auto* last = ctx.lastAttempt();
        if (!last) return false;
        return codes.count(static_cast<int>(last->response.status)) > 0;
    };
}

// SAFINAE predictor for RetryConditionFn
template <class Fn>
using is_retry_pred = std::is_invocable_r<bool, Fn&, const RetryContext&>;

/**
 * Combine multiple conditions with OR logic.
 * Returns true if any condition returns true.
 */
template <class... Fns,
          std::enable_if_t<
              (sizeof...(Fns) > 0) &&
              (is_retry_pred<std::decay_t<Fns>>::value && ...) &&
              (std::is_copy_constructible_v<std::decay_t<Fns>> && ...),
              int> = 0>
inline RetryConditionFn anyOf(Fns&&... fns) {
    return [fs = std::tuple<std::decay_t<Fns>...>(std::forward<Fns>(fns)...)]
           (const RetryContext& ctx) -> bool {
        return std::apply(
            [&](auto&... g) { return ((static_cast<bool>(g(ctx))) || ...); },
            fs
        );
    };
}

/**
 * Combine multiple conditions with AND logic.
 * Returns true if all conditions return true (or are empty).
 */
template <class... Fns,
          std::enable_if_t<
              (sizeof...(Fns) > 0) &&
              (is_retry_pred<std::decay_t<Fns>>::value && ...) &&
              (std::is_copy_constructible_v<std::decay_t<Fns>> && ...),
              int> = 0>
inline RetryConditionFn allOf(Fns&&... fns) {
    return [fs = std::tuple<std::decay_t<Fns>...>(std::forward<Fns>(fns)...)]
           (const RetryContext& ctx) -> bool {
        return std::apply(
            [&](auto&... g) { return ((static_cast<bool>(g(ctx))) && ...); },
            fs
        );
    };
}

// ============ Backoff Strategies ============
// All return absolute timestamp in seconds based on lastCompleteAt()

/**
 * Exponential backoff with optional jitter.
 * delay = min(baseDelay * multiplier^attempt, maxDelay) + jitter
 * Returns: lastCompleteAt + delay
 */
inline BackoffScheduleFn exponentialBackoff(
    double baseDelay = 0.1,      // 100ms in seconds
    double maxDelay = 30.0,      // 30 seconds
    double multiplier = 2.0,
    double jitterFactor = 0.3)
{
    return [=](const RetryContext& ctx) -> double {
        uint32_t attempt = ctx.attemptCount();  // Number of attempts so far
        double delay = baseDelay * std::pow(multiplier, static_cast<double>(attempt));
        delay = std::min(delay, maxDelay);

        if (jitterFactor > 0) {
            double jitter = util::jitter_generator(delay * jitterFactor);
            delay += jitter;
            delay = std::max(0.0, delay);
        }

        return ctx.lastCompleteAt() + delay;
    };
}

/**
 * Fixed delay between retries.
 * Returns: lastCompleteAt + delay
 */
inline BackoffScheduleFn fixedDelay(double delay = 1.0) {  // 1 second default
    return [delay](const RetryContext& ctx) -> double {
        return ctx.lastCompleteAt() + delay;
    };
}

/**
 * Linear backoff: delay increases linearly with each attempt.
 * delay = min(initialDelay + increment * attempt, maxDelay)
 * Returns: lastCompleteAt + delay
 */
inline BackoffScheduleFn linearBackoff(
    double initialDelay = 0.1,   // 100ms in seconds
    double increment = 0.1,      // 100ms increment
    double maxDelay = 5.0)       // 5 seconds max
{
    return [=](const RetryContext& ctx) -> double {
        uint32_t attempt = ctx.attemptCount();
        double delay = initialDelay + increment * static_cast<double>(attempt);
        delay = std::min(delay, maxDelay);
        return ctx.lastCompleteAt() + delay;
    };
}

/**
 * Immediate retry - no delay.
 * Returns: lastCompleteAt (retry immediately after completion)
 */
inline BackoffScheduleFn immediate() {
    return [](const RetryContext& ctx) -> double {
        return ctx.lastCompleteAt();  // Retry immediately after completion
    };
}

} // namespace retry

// Default constructor implementation for RetryPolicy
inline RetryPolicy::RetryPolicy()
    : maxRetries(3)
    , totalTimeout(0)
    , shouldRetry(retry::anyOf(retry::defaultCondition(), retry::httpStatusCondition()))
    , getNextRetryTime(retry::exponentialBackoff())
{}

} // namespace http_client
