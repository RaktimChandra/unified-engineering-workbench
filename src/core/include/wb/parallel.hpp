// Batch thermal solves: reproducible case generator, serial path and a bounded
// std::thread worker pool (Task D).
#pragma once

#include "wb/common.hpp"
#include "wb/fea1d.hpp"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace wb::par {

struct BatchSpec {
    std::uint64_t seed = 20261101;
    std::size_t cases = 4000;
    std::size_t elements = 4000;   ///< mesh size per solve
    double k_min = 0.5, k_max = 400.0;   ///< W/(m K), positive
    double q_min = 1e3, q_max = 1e5;     ///< W/m^3, positive
};

struct BatchCase {
    std::size_t index = 0;
    fea::RodProblem problem;
};

/// Deterministic on every standard library: std::mt19937_64's output sequence
/// is fully specified by the C++ standard, but std::uniform_real_distribution
/// is not (libstdc++ and MSVC STL produce different doubles from the same
/// engine). We therefore map raw 64-bit words to [0,1) ourselves.
std::vector<BatchCase> generate(const BatchSpec& spec);
double unit_double(std::uint64_t word) noexcept;

/// Results stored case-major: temperatures[case * (elements+1) + node].
struct BatchResult {
    std::size_t cases = 0, nodes = 0;
    std::vector<double> temperatures;
    double max_temperature(std::size_t c) const;
};

/// Fixed-size pool of std::threads. Threads are created once and parked on a
/// condition variable, so pool start-up cost is excluded from timed runs.
class WorkerPool {
public:
    explicit WorkerPool(std::size_t workers);
    ~WorkerPool();
    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    std::size_t size() const noexcept { return threads_.size(); }

    /// Runs body(i, worker_id) for i in [0, count). Work is handed out in
    /// chunks via an atomic counter (dynamic scheduling). Blocks until done.
    /// If `cancel` fires, remaining chunks are skipped and Cancelled is thrown.
    void parallel_for(std::size_t count, std::size_t chunk,
                      const std::function<void(std::size_t, std::size_t)>& body,
                      const CancelToken* cancel = nullptr);

private:
    void worker_main(std::size_t id);

    std::vector<std::thread> threads_;
    std::mutex mu_;
    std::condition_variable wake_, done_;
    const std::function<void(std::size_t, std::size_t)>* body_ = nullptr;
    const CancelToken* cancel_ = nullptr;
    std::size_t count_ = 0, chunk_ = 1;
    std::atomic<std::size_t> next_{0};
    std::size_t generation_ = 0, finished_ = 0;
    bool stop_ = false;
};

/// Allocates (or reuses) `out` with the right shape. Kept separate from the
/// solve loops so benchmarks can exclude allocation and first-touch page
/// faults from the timed region.
void prepare(BatchResult& out, std::size_t cases, std::size_t elements);

void run_serial_into(const std::vector<BatchCase>& cases, std::size_t elements, BatchResult& out,
                     const CancelToken* cancel = nullptr);
void run_parallel_into(WorkerPool& pool, const std::vector<BatchCase>& cases, std::size_t elements,
                       BatchResult& out, const CancelToken* cancel = nullptr);

/// Convenience wrappers that allocate a fresh result.
BatchResult run_serial(const std::vector<BatchCase>& cases, std::size_t elements,
                       const CancelToken* cancel = nullptr);
BatchResult run_parallel(WorkerPool& pool, const std::vector<BatchCase>& cases, std::size_t elements,
                         const CancelToken* cancel = nullptr);

/// max |a - b| over all nodal temperatures; requires identical shapes.
double max_abs_difference(const BatchResult& a, const BatchResult& b);

} // namespace wb::par
