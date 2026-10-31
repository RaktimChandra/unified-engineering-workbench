#include "wb/benchmark.hpp"

#include <algorithm>
#include <chrono>
#include <memory>

namespace wb::bench {

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

Report run(const Config& cfg_in, const ProgressFn& progress, const CancelToken* cancel) {
    using clock = std::chrono::steady_clock;
    Report rep;
    rep.host = sys::query();
    rep.config = cfg_in;
    const unsigned logical = std::max(1u, rep.host.logical_cpus);
    rep.config.workers.clear();
    for (std::size_t w : cfg_in.workers) {
        if (w == 0) continue;
        if (w <= logical) rep.config.workers.push_back(w);
        else rep.skipped_workers.push_back(w);
    }
    if (rep.config.workers.empty()) rep.config.workers.push_back(1);

    auto t0 = clock::now();
    const auto cases = par::generate(cfg_in.batch);
    rep.generate_s = std::chrono::duration<double>(clock::now() - t0).count();
    const std::size_t ne = cfg_in.batch.elements;

    const int reps = std::max(1, cfg_in.measured_runs);
    const int warm = std::max(0, cfg_in.warmup_runs);
    const double total_steps = static_cast<double>((1 + rep.config.workers.size()) * (reps + warm));
    double step = 0;
    auto tick = [&](const std::string& stage) {
        step += 1;
        if (progress) progress(step / total_steps, stage);
    };

    // Reference: plain serial loop (no pool, no threads).
    Row serial;
    serial.variant = "serial";
    // Output buffers are allocated and first touched before timing starts (and
    // during the warm-up run), so the timed region measures solving, not
    // page-fault handling for a freshly mapped multi-megabyte array.
    par::BatchResult reference, work;
    par::prepare(reference, cases.size(), ne);
    par::prepare(work, cases.size(), ne);
    std::fill(work.temperatures.begin(), work.temperatures.end(), 0.0);
    for (int r = 0; r < warm + reps; ++r) {
        throw_if_cancelled(cancel);
        auto s = clock::now();
        par::run_serial_into(cases, ne, r == 0 ? reference : work, cancel);
        const double dt = std::chrono::duration<double>(clock::now() - s).count();
        if (r >= warm) serial.seconds.push_back(dt);
        tick(r < warm ? "serial warm-up" : "serial run " + std::to_string(r - warm + 1));
    }
    serial.median_s = median(serial.seconds);
    serial.speedup = 1.0;
    serial.efficiency = 1.0;
    rep.rows.push_back(serial);

    rep.agreement_ok = true;
    for (std::size_t p : rep.config.workers) {
        Row row;
        row.variant = "pool";
        row.workers = p;
        par::WorkerPool pool(p); // thread creation happens here, outside the timed region
        double worst = 0;
        for (int r = 0; r < warm + reps; ++r) {
            throw_if_cancelled(cancel);
            std::fill(work.temperatures.begin(), work.temperatures.end(), -1.0); // stale data cannot pass the check
            auto s = clock::now();
            par::run_parallel_into(pool, cases, ne, work, cancel);
            const double dt = std::chrono::duration<double>(clock::now() - s).count();
            if (r >= warm) row.seconds.push_back(dt);
            worst = std::max(worst, par::max_abs_difference(reference, work)); // outside timing
            tick(std::to_string(p) + "-worker " + (r < warm ? "warm-up" : "run " + std::to_string(r - warm + 1)));
        }
        row.median_s = median(row.seconds);
        row.speedup = serial.median_s / row.median_s;
        row.efficiency = row.speedup / static_cast<double>(p);
        row.max_abs_diff_c = worst;
        if (worst > rep.tolerance_c) rep.agreement_ok = false;
        rep.rows.push_back(row);
    }
    if (logical == 1)
        rep.note = "Only one logical CPU is available: the parallel path was executed and verified, "
                   "but speedup cannot be assessed on this machine.";
    else if (rep.host.under_wine)
        rep.note = "Windows build measured under Wine; timings are indicative only.";
    return rep;
}

} // namespace wb::bench
