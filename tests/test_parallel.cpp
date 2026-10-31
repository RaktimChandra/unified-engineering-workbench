#include <doctest/doctest.h>

#include "wb/benchmark.hpp"
#include "wb/parallel.hpp"

#include <atomic>
#include <set>
#include <mutex>
#include <random>
#include <thread>

using namespace wb::par;

TEST_SUITE("parallel") {

TEST_CASE("unit_double maps to [0,1) and is exact") {
    CHECK(unit_double(0) == 0.0);
    CHECK(unit_double(~0ull) < 1.0);
    CHECK(unit_double(1ull << 63) == 0.5);
}

TEST_CASE("generator is reproducible, positive and seed dependent") {
    BatchSpec s;
    s.cases = 500;
    const auto a = generate(s), b = generate(s);
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].index == i);
        CHECK(a[i].problem.conductivity == b[i].problem.conductivity);
        CHECK(a[i].problem.heat_gen == b[i].problem.heat_gen);
        CHECK(a[i].problem.conductivity > 0);
        CHECK(a[i].problem.heat_gen > 0);
    }
    s.seed += 1;
    CHECK(generate(s)[0].problem.conductivity != a[0].problem.conductivity);
}

TEST_CASE("golden values: same cases on every standard library") {
    // mt19937_64 with seed 20261101 is fully specified by the C++ standard and
    // our mapping avoids implementation-defined distributions, so these exact
    // doubles must be reproduced by GCC, Clang and MSVC builds alike.
    BatchSpec s;
    s.cases = 3;
    const auto c = generate(s);
    std::mt19937_64 rng(s.seed);
    const double u0 = unit_double(rng()), u1 = unit_double(rng());
    CHECK(c[0].problem.conductivity == s.k_min + (s.k_max - s.k_min) * u0);
    CHECK(c[0].problem.heat_gen == s.q_min + (s.q_max - s.q_min) * u1);
    // The 10000th output of a default-seeded mt19937_64 is fixed by [rand.predef].
    std::mt19937_64 std_check;
    std_check.discard(9999);
    CHECK(std_check() == 9981545732273789042ull);
}

TEST_CASE("worker pool visits every index exactly once, reusable across calls") {
    WorkerPool pool(3);
    for (int round = 0; round < 5; ++round) {
        std::vector<std::atomic<int>> hits(1000);
        pool.parallel_for(hits.size(), 7, [&](std::size_t i, std::size_t w) {
            CHECK(w < 3);
            hits[i].fetch_add(1);
        });
        for (auto& h : hits) CHECK(h.load() == 1);
    }
}

TEST_CASE("work really runs on several threads") {
    WorkerPool pool(2);
    std::mutex mu;
    std::set<std::thread::id> ids;
    pool.parallel_for(64, 1, [&](std::size_t, std::size_t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        std::lock_guard<std::mutex> lk(mu);
        ids.insert(std::this_thread::get_id());
    });
    CHECK(ids.size() == 2);
    CHECK(ids.count(std::this_thread::get_id()) == 0); // not executed on the caller
}

TEST_CASE("serial and parallel batches agree bitwise and keep input order") {
    BatchSpec s;
    s.cases = 300;
    s.elements = 257;
    const auto cases = generate(s);
    const auto serial = run_serial(cases, s.elements);
    for (std::size_t workers : {1, 2, 4}) {
        WorkerPool pool(workers);
        const auto par = run_parallel(pool, cases, s.elements);
        CHECK(max_abs_difference(serial, par) == 0.0);
    }
    // Spot-check order: case i holds the solution of problem i.
    const auto ref = wb::fea::solve(cases[123].problem, s.elements);
    CHECK(serial.temperatures[123 * serial.nodes + 100] == ref.T[100]);
}

TEST_CASE("cancellation stops a parallel batch with Cancelled") {
    WorkerPool pool(2);
    wb::CancelToken t;
    t.request();
    BatchSpec s;
    s.cases = 1000;
    const auto cases = generate(s);
    CHECK_THROWS_AS(run_parallel(pool, cases, 100, &t), wb::Cancelled);
    CHECK_THROWS_AS(run_serial(cases, 100, &t), wb::Cancelled);
    // The pool remains usable after a cancelled run.
    const auto ok = run_parallel(pool, cases, 50);
    CHECK(ok.cases == 1000);
}

TEST_CASE("benchmark harness: median, capping and agreement") {
    CHECK(wb::bench::median({3, 1, 2}) == 2);
    CHECK(wb::bench::median({4, 1, 2, 3}) == 2.5);
    wb::bench::Config cfg;
    cfg.batch.cases = 64;
    cfg.batch.elements = 64;
    cfg.workers = {1, 2, 4, 1024};
    cfg.measured_runs = 3;
    const auto rep = wb::bench::run(cfg);
    CHECK(rep.agreement_ok);
    CHECK(rep.skipped_workers.back() == 1024);
    CHECK(rep.rows.front().variant == "serial");
    CHECK(rep.rows.front().seconds.size() == 3);
    for (const auto& r : rep.rows) CHECK(r.max_abs_diff_c <= 1e-8);
}

TEST_CASE("invalid batch specifications are rejected") {
    BatchSpec s;
    s.cases = 0;
    CHECK_THROWS(generate(s));
    BatchSpec k;
    k.k_min = -1;
    CHECK_THROWS(generate(k));
    CHECK_THROWS(WorkerPool(0));
}

} // TEST_SUITE
