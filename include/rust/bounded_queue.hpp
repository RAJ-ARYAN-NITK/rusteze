#pragma once
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>

namespace rust {

// Thread-safe bounded queue. Blocking push (backpressure) or drop-oldest push (live sources).
template <class T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity) : cap_(capacity) {}

    bool push(T v, bool dropOldest = false) {
        std::unique_lock<std::mutex> lk(m_);
        if (dropOldest) {
            if (closed_) return false;
            while (q_.size() >= cap_) { q_.pop_front(); ++dropped_; }
        } else {
            notFull_.wait(lk, [&] { return closed_ || q_.size() < cap_; });
            if (closed_) return false;
        }
        q_.push_back(std::move(v));
        notEmpty_.notify_one();
        return true;
    }

    // Blocks until an item is available; returns nullopt once closed and drained.
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lk(m_);
        notEmpty_.wait(lk, [&] { return closed_ || !q_.empty(); });
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop_front();
        notFull_.notify_one();
        return v;
    }

    void close() {
        { std::lock_guard<std::mutex> lk(m_); closed_ = true; }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    std::size_t dropped() const { std::lock_guard<std::mutex> lk(m_); return dropped_; }

private:
    mutable std::mutex m_;
    std::condition_variable notEmpty_, notFull_;
    std::deque<T> q_;
    std::size_t cap_, dropped_ = 0;
    bool closed_ = false;
};

}  // namespace rust
