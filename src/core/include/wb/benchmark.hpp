// Timing harness for Task D: warm-up + repeated measurements, median, speedup.
#pragma once

#include "wb/common.hpp"
#include "wb/parallel.hpp"
#include "wb/sysinfo.hpp"

#include <vector>

namespace wb::bench {

struct Config {
    par::BatchSpec batch;
    std::vector<std::size_t> workers = {1, 2, 4}; ///< capped at logical CPUs at run time
    int warmup_runs = 1;
    int measured_runs = 5;
};

struct Row {
    std::string variant;          ///< "serial" or "pool"
    std::size_t workers = 1;
    std::vector<double> seconds;  ///< each measured repetition (wall clock, compute only)
    double median_s = 0;
    double speedup = 0;           ///< T_serial_median / T_p_median
    double efficiency = 0;        ///< speedup / p
    double max_abs_diff_c = 0;    ///< vs serial result, all nodes, all cases
};

struct Report {
    sys::HostInfo host;
    Config config;                    ///< with workers already capped
    std::vector<std::size_t> skipped_workers; ///< requested but above logical CPU count
    double generate_s = 0;            ///< case generation time (excluded from compute timings)
    std::vector<Row> rows;
    bool agreement_ok = false;        ///< every row within tolerance
    double tolerance_c = 1e-8;
    std::string note;
};

double median(std::vector<double> v);

/// Runs the full benchmark. Progress reports (0..1); cancellation throws wb::Cancelled.
Report run(const Config& cfg, const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);

} // namespace wb::bench
