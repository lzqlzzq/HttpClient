#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace http_client {

class CumulativeCounter {
public:
    uint64_t update(uint64_t current) noexcept {
        const uint64_t delta = current >= previous_ ? current - previous_ : current;
        previous_ = current;
        return delta;
    }

    void reset() noexcept {
        previous_ = 0;
    }

private:
    uint64_t previous_ = 0;
};

class ThroughputTracker {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit ThroughputTracker(uint32_t windowSeconds, TimePoint startedAt = Clock::now())
        : windowSeconds_(windowSeconds), buckets_(static_cast<size_t>(windowSeconds) + 1), startedAt_(startedAt) {
        if (windowSeconds == 0)
            throw std::invalid_argument("speedAverageWindowSeconds must be greater than zero");
    }

    void record(uint64_t uplinkBytes, uint64_t downlinkBytes, TimePoint now = Clock::now()) {
        const auto bucketNumber = getBucketNumber(now);
        if (bucketNumber < 0 || (uplinkBytes == 0 && downlinkBytes == 0))
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        Bucket& bucket = buckets_[static_cast<size_t>(bucketNumber) % buckets_.size()];
        if (bucket.number != bucketNumber)
            bucket = Bucket{bucketNumber};

        if (uplinkBytes != 0) {
            bucket.uplinkBytes = saturatingAdd(bucket.uplinkBytes, uplinkBytes);
            bucket.lastUplinkAt = now;
            bucket.hasUplink = true;
        }
        if (downlinkBytes != 0) {
            bucket.downlinkBytes = saturatingAdd(bucket.downlinkBytes, downlinkBytes);
            bucket.lastDownlinkAt = now;
            bucket.hasDownlink = true;
        }
    }

    double uplinkSpeed(TimePoint now = Clock::now()) const {
        return average(now, Direction::Uplink);
    }

    double downlinkSpeed(TimePoint now = Clock::now()) const {
        return average(now, Direction::Downlink);
    }

    double peakUplinkSpeed(TimePoint now = Clock::now()) const {
        return peak(now, Direction::Uplink);
    }

    double peakDownlinkSpeed(TimePoint now = Clock::now()) const {
        return peak(now, Direction::Downlink);
    }

private:
    struct Bucket {
        explicit Bucket(int64_t bucketNumber = -1) : number(bucketNumber) {}

        int64_t number;
        uint64_t uplinkBytes = 0;
        uint64_t downlinkBytes = 0;
        TimePoint lastUplinkAt{};
        TimePoint lastDownlinkAt{};
        bool hasUplink = false;
        bool hasDownlink = false;
    };

    enum class Direction { Uplink, Downlink };

    static uint64_t saturatingAdd(uint64_t lhs, uint64_t rhs) noexcept {
        const uint64_t limit = std::numeric_limits<uint64_t>::max();
        return rhs > limit - lhs ? limit : lhs + rhs;
    }

    int64_t getBucketNumber(TimePoint now) const noexcept {
        if (now < startedAt_)
            return -1;
        return std::chrono::duration_cast<std::chrono::seconds>(now - startedAt_).count();
    }

    double average(TimePoint now, Direction direction) const {
        const double elapsedSeconds = std::chrono::duration<double>(now - startedAt_).count();
        if (elapsedSeconds < 0.0)
            return 0.0;

        const double denominator = std::min<double>(
            windowSeconds_, std::max(1.0, elapsedSeconds));
        const TimePoint oldest = now - std::chrono::seconds(windowSeconds_);
        uint64_t bytes = 0;

        std::lock_guard<std::mutex> lock(mutex_);
        for (const Bucket& bucket : buckets_) {
            const bool hasData = direction == Direction::Uplink ? bucket.hasUplink : bucket.hasDownlink;
            const TimePoint lastAt = direction == Direction::Uplink ? bucket.lastUplinkAt : bucket.lastDownlinkAt;
            if (!hasData || lastAt <= oldest)
                continue;

            const uint64_t value = direction == Direction::Uplink ? bucket.uplinkBytes : bucket.downlinkBytes;
            bytes = saturatingAdd(bytes, value);
        }
        return static_cast<double>(bytes) / denominator;
    }

    double peak(TimePoint now, Direction direction) const {
        const int64_t currentBucket = getBucketNumber(now);
        if (currentBucket < 0)
            return 0.0;

        const TimePoint oldest = now - std::chrono::seconds(windowSeconds_);
        uint64_t maximum = 0;

        std::lock_guard<std::mutex> lock(mutex_);
        for (const Bucket& bucket : buckets_) {
            const bool hasData = direction == Direction::Uplink ? bucket.hasUplink : bucket.hasDownlink;
            const TimePoint lastAt = direction == Direction::Uplink ? bucket.lastUplinkAt : bucket.lastDownlinkAt;
            if (!hasData || bucket.number >= currentBucket || lastAt <= oldest)
                continue;

            const uint64_t value = direction == Direction::Uplink ? bucket.uplinkBytes : bucket.downlinkBytes;
            maximum = std::max(maximum, value);
        }
        return static_cast<double>(maximum);
    }

    const uint32_t windowSeconds_;
    std::vector<Bucket> buckets_;
    const TimePoint startedAt_;
    mutable std::mutex mutex_;
};

} // namespace http_client
