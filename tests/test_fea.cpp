#include <doctest/doctest.h>

#include "wb/fea1d.hpp"

#include <cmath>
#include <stdexcept>

using namespace wb::fea;

TEST_SUITE("fea1d") {

TEST_CASE("exact solution satisfies the BCs and the ODE") {
    const RodProblem p;
    CHECK(exact_temperature(p, 0.0) == doctest::Approx(100.0));
    CHECK(exact_temperature(p, 1.0) == doctest::Approx(20.0));
    // -k T'' = q  ->  T'' = -q/k = -1000 K/m^2, checked by a centred difference.
    const double h = 1e-3, x = 0.3;
    const double d2 = (exact_temperature(p, x + h) - 2 * exact_temperature(p, x) + exact_temperature(p, x - h)) / (h * h);
    CHECK(d2 == doctest::Approx(-1000.0).epsilon(1e-6));
    // Peak where T' = 0: x* = 0.42 m, T = 188.2 C.
    CHECK(exact_gradient(p, 0.42) == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(exact_temperature(p, 0.42) == doctest::Approx(188.2));
}

TEST_CASE("element assembly: stiffness and load entries for N=4") {
    const RodProblem p;
    const auto a = assemble(p, 4);
    const double ke = 10 * 1e-4 / 0.25; // kA/h = 4e-3 W/K
    const double fe = 0.5 * 1e4 * 1e-4 * 0.25; // qAh/2 = 0.125 W
    CHECK(a.K.diag[0] == doctest::Approx(ke));
    CHECK(a.K.diag[2] == doctest::Approx(2 * ke));
    CHECK(a.K.sup[1] == doctest::Approx(-ke));
    CHECK(a.K.sub[3] == doctest::Approx(-ke));
    CHECK(a.F[0] == doctest::Approx(fe));
    CHECK(a.F[2] == doctest::Approx(2 * fe));
    double total = 0;
    for (double f : a.F) total += f;
    CHECK(total == doctest::Approx(1e4 * 1e-4 * 1.0)); // q A L = 1 W
    // Each row of the unconstrained K sums to zero (constant T is a null mode).
    for (std::size_t i = 0; i < 5; ++i) {
        const double row = a.K.diag[i] + (i > 0 ? a.K.sub[i] : 0) + (i < 4 ? a.K.sup[i] : 0);
        CHECK(row == doctest::Approx(0.0).epsilon(1e-15));
    }
}

TEST_CASE("Dirichlet BCs are imposed exactly") {
    for (std::size_t n : {1, 2, 3, 10, 80}) {
        const auto s = solve(RodProblem{}, n);
        CHECK(s.T.front() == 100.0);
        CHECK(s.T.back() == 20.0);
        CHECK(s.x.back() == 1.0);
    }
}

TEST_CASE("nodal values are exact; midpoint error equals q h^2 / (8k)") {
    const RodProblem p;
    for (std::size_t n : {10, 20, 40, 80}) {
        const auto s = solve(p, n);
        const auto e = evaluate(p, s);
        CAPTURE(n);
        CHECK(e.max_nodal_error < 1e-11);
        const double h = 1.0 / static_cast<double>(n);
        CHECK(e.max_midpoint_error == doctest::Approx(p.heat_gen * h * h / (8 * p.conductivity)).epsilon(1e-9));
        // The interpolation error is a parabola of height q h^2/(8k) per element,
        // so the dense-sample maximum coincides with the midpoint value.
        CHECK(e.max_sampled_error == doctest::Approx(e.max_midpoint_error).epsilon(1e-9));
        // Closed form L2 norm: sqrt(L) * (q/2k) * h^2 / sqrt(30).
        CHECK(e.l2_error == doctest::Approx(500.0 * h * h / std::sqrt(30.0)).epsilon(1e-9));
    }
}

TEST_CASE("observed convergence rate is 2 for midpoint and L2 errors") {
    const auto rows = convergence_study(RodProblem{});
    REQUIRE(rows.size() == 4);
    for (std::size_t i = 1; i < rows.size(); ++i) {
        CHECK(rows[i].midpoint_rate == doctest::Approx(2.0).epsilon(1e-8));
        CHECK(rows[i].l2_rate == doctest::Approx(2.0).epsilon(1e-8));
    }
    CHECK(rows[0].err.max_midpoint_error == doctest::Approx(1.25));
    CHECK(rows[3].err.max_midpoint_error == doctest::Approx(0.01953125));
}

TEST_CASE("energy balance: boundary reactions remove exactly q A L") {
    const RodProblem p;
    const auto s = solve(p, 40);
    CHECK(std::abs(s.heat_balance_residual) < 1e-12);
    // R = K T - F is the heat *supplied* at a constrained node. Fourier flux is
    // q_x = -k T', so the inflow at x = 0 is +q_x A = -k A T'(0) and at x = L it
    // is -q_x A = +k A T'(L). Both are negative here: heat leaves through both ends.
    CHECK(s.reaction_left == doctest::Approx(-p.conductivity * p.area * exact_gradient(p, 0.0)).epsilon(1e-9));
    CHECK(s.reaction_right == doctest::Approx(p.conductivity * p.area * exact_gradient(p, 1.0)).epsilon(1e-9));
    CHECK(s.reaction_left == doctest::Approx(-0.42));
    CHECK(s.reaction_right == doctest::Approx(-0.58));
}

TEST_CASE("Thomas solver agrees with dense Gaussian elimination") {
    Tridiagonal A;
    A.sub = {0, -1, -2, -1, -3};
    A.diag = {4, 5, 6, 5, 7};
    A.sup = {-1, -2, -1, -3, 0};
    const std::vector<double> b = {1, 2, 3, 4, 5};
    std::vector<std::vector<double>> D(5, std::vector<double>(5, 0));
    for (int i = 0; i < 5; ++i) {
        D[i][i] = A.diag[i];
        if (i > 0) D[i][i - 1] = A.sub[i];
        if (i < 4) D[i][i + 1] = A.sup[i];
    }
    const auto x1 = thomas(A, b), x2 = dense_gauss(D, b);
    for (int i = 0; i < 5; ++i) CHECK(x1[i] == doctest::Approx(x2[i]).epsilon(1e-13));
}

TEST_CASE("fused batch kernel is bitwise identical to the reference solver") {
    Scratch sc;
    for (std::size_t n : {1, 2, 3, 7, 64, 500}) {
        RodProblem p;
        p.conductivity = 3.7;
        p.heat_gen = 51234.5;
        const auto ref = solve(p, n);
        std::vector<double> out(n + 1);
        solve_into(p, n, out.data(), sc);
        for (std::size_t i = 0; i <= n; ++i) CHECK(out[i] == ref.T[i]);
    }
}

TEST_CASE("interpolation reproduces nodes and midpoints") {
    const RodProblem p;
    const auto s = solve(p, 10);
    CHECK(interpolate(s, 0.3) == doctest::Approx(s.T[3]));
    CHECK(interpolate(s, 0.35) == doctest::Approx(0.5 * (s.T[3] + s.T[4])));
    CHECK(interpolate(s, -1) == s.T.front());
    CHECK(interpolate(s, 2) == s.T.back());
}

TEST_CASE("zero heat generation gives a linear profile with zero error") {
    RodProblem p;
    p.heat_gen = 0;
    const auto e = evaluate(p, solve(p, 3));
    CHECK(e.max_sampled_error < 1e-12);
}

TEST_CASE("invalid problems are rejected with actionable messages") {
    RodProblem p;
    p.conductivity = -1;
    CHECK(p.validate().find("Conductivity") != std::string::npos);
    CHECK_THROWS_AS(solve(p, 10), std::invalid_argument);
    RodProblem q;
    q.length = std::nan("");
    CHECK_FALSE(q.validate().empty());
    RodProblem t;
    t.t_left = -300;
    CHECK(t.validate().find("absolute zero") != std::string::npos);
    CHECK_THROWS_AS(assemble(RodProblem{}, 0), std::invalid_argument);
}

} // TEST_SUITE
