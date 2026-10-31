#include "wb/parallel.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <stdexcept>

namespace wb::par {

double unit_double(std::uint64_t word) noexcept {
    // Top 53 bits -> exactly representable double in [0, 1).
    return static_cast<double>(word >> 11) * 0x1.0p-53;
}

std::vector<BatchCase> generate(const BatchSpec& spec) {
    if (spec.cases == 0) throw std::invalid_argument("case count must be >= 1");
    if (spec.elements < 1) throw std::invalid_argument("mesh size must be >= 1 element");
    if (!(spec.k_min > 0 && spec.k_max >= spec.k_min)) throw std::invalid_argument("k range must be positive");
    if (!(spec.q_min > 0 && spec.q_max >= spec.q_min)) throw std::invalid_argument("q range must be positive");
    std::mt19937_64 rng(spec.seed);
    std::vector<BatchCase> out(spec.cases);
    for (std::size_t i = 0; i < spec.cases; ++i) {
        out[i].index = i;
        fea::RodProblem& p = out[i].problem; // L, A and BCs keep the Task C defaults
        // Linear mapping on purpose: a log-uniform draw would need std::pow,
        // whose last bit may differ between C runtimes and would silently break
        // the "same cases on every compiler" guarantee.
        const double uk = unit_double(rng());
        const double uq = unit_double(rng());
        p.conductivity = spec.k_min + (spec.k_max - spec.k_min) * uk;
        p.heat_gen = spec.q_min + (spec.q_max - spec.q_min) * uq;
    }
    return out;
}

double BatchResult::max_temperature(std::size_t c) const {
    const double* t = &temperatures[c * nodes];
    return *std::max_element(t, t + nodes);
}

WorkerPool::WorkerPool(std::size_t workers) {
    if (workers == 0) throw std::invalid_argument("worker pool needs at least one thread");
    threads_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) threads_.emplace_back([this, i] { worker_main(i); });
}

WorkerPool::~WorkerPool() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    wake_.notify_all();
    for (auto& t : threads_) t.join();
}

void WorkerPool::worker_main(std::size_t id) {
    std::size_t seen_generation = 0;
    for (;;) {
        const std::function<void(std::size_t, std::size_t)>* body;
        const CancelToken* cancel;
        std::size_t count, chunk;
        {
            std::unique_lock<std::mutex> lk(mu_);
            wake_.wait(lk, [&] { return stop_ || generation_ != seen_generation; });
            if (stop_) return;
            seen_generation = generation_;
            body = body_;
            cancel = cancel_;
            count = count_;
            chunk = chunk_;
        }
        for (;;) {
            if (cancel != nullptr && cancel->requested()) break;
            const std::size_t begin = next_.fetch_add(chunk, std::memory_order_relaxed);
            if (begin >= count) break;
            const std::size_t end = std::min(count, begin + chunk);
            for (std::size_t i = begin; i < end; ++i) (*body)(i, id);
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (++finished_ == threads_.size()) done_.notify_one();
        }
    }
}

void WorkerPool::parallel_for(std::size_t count, std::size_t chunk,
                              const std::function<void(std::size_t, std::size_t)>& body,
                              const CancelToken* cancel) {
    std::unique_lock<std::mutex> lk(mu_);
    body_ = &body;
    cancel_ = cancel;
    count_ = count;
    chunk_ = std::max<std::size_t>(1, chunk);
    next_.store(0, std::memory_order_relaxed);
    finished_ = 0;
    ++generation_;
    wake_.notify_all();
    done_.wait(lk, [&] { return finished_ == threads_.size(); });
    body_ = nullptr;
    lk.unlock();
    throw_if_cancelled(cancel);
}

namespace {
BatchResult make_result(std::size_t cases, std::size_t elements) {
    BatchResult r;
    r.cases = cases;
    r.nodes = elements + 1;
    r.temperatures.assign(cases * r.nodes, 0.0);
    return r;
}
} // namespace

BatchResult run_serial(const std::vector<BatchCase>& cases, std::size_t elements, const CancelToken* cancel) {
    BatchResult r = make_result(cases.size(), elements);
    fea::Scratch scratch;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        if ((i & 63) == 0) throw_if_cancelled(cancel);
        fea::solve_into(cases[i].problem, elements, &r.temperatures[i * r.nodes], scratch);
    }
    return r;
}

BatchResult run_parallel(WorkerPool& pool, const std::vector<BatchCase>& cases, std::size_t elements,
                         const CancelToken* cancel) {
    BatchResult r = make_result(cases.size(), elements);
    // One scratch buffer per worker: no locks, no shared mutable state. Each
    // case writes only its own slice of r.temperatures, so output order equals
    // input order regardless of which thread solved it.
    std::vector<fea::Scratch> scratch(pool.size());
    const std::size_t chunk = std::max<std::size_t>(1, cases.size() / (pool.size() * 16));
    pool.parallel_for(
        cases.size(), chunk,
        [&](std::size_t i, std::size_t worker) {
            fea::solve_into(cases[i].problem, elements, &r.temperatures[i * r.nodes], scratch[worker]);
        },
        cancel);
    return r;
}

double max_abs_difference(const BatchResult& a, const BatchResult& b) {
    if (a.temperatures.size() != b.temperatures.size()) throw std::invalid_argument("result shapes differ");
    double m = 0;
    for (std::size_t i = 0; i < a.temperatures.size(); ++i)
        m = std::max(m, std::abs(a.temperatures[i] - b.temperatures[i]));
    return m;
}

} // namespace wb::par
