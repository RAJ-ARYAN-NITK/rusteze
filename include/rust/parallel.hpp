#pragma once
#include <atomic>
#include <cstddef>
#include <thread>
#include <vector>

namespace rust {

// Runs f(i) for i in [0, n) on `threads` workers. Dynamic scheduling via an atomic counter.
template <class F>
void parallelFor(std::size_t n, unsigned threads, F f) {
    if (threads == 0) threads = 1;
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    for (unsigned t = 0; t < threads; ++t)
        pool.emplace_back([&] {
            for (std::size_t i; (i = next.fetch_add(1)) < n;) f(i);
        });
    for (auto& th : pool) th.join();
}

}  // namespace rust
