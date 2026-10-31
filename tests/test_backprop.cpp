#include <doctest/doctest.h>

#include "wb/backprop.hpp"

#include <cmath>

using namespace wb::bp;

TEST_SUITE("backprop") {

TEST_CASE("forward pass matches the hand calculation") {
    const auto f = forward(Params{}, 1, 2);
    CHECK(f.z == 0.5);
    CHECK(f.h == doctest::Approx(0.46211715726).epsilon(1e-10));
    CHECK(f.y_hat == doctest::Approx(0.46211715726).epsilon(1e-10));
    CHECK(f.error == doctest::Approx(-1.53788284274).epsilon(1e-10));
    CHECK(f.loss == doctest::Approx(1.18254182).epsilon(1e-8));
}

TEST_CASE("analytic gradients match the handwritten values") {
    const auto g = gradients(Params{}, 1, 2);
    CHECK(g.dw == doctest::Approx(-1.20946448).epsilon(1e-8));
    CHECK(g.db == doctest::Approx(-1.20946448).epsilon(1e-8));
    CHECK(g.dv == doctest::Approx(-0.71068205).epsilon(1e-8));
    CHECK(g.dc == doctest::Approx(-1.53788284).epsilon(1e-8));
}

TEST_CASE("central finite differences agree within 1e-5 (eps = 1e-6)") {
    const auto a = gradients(Params{}, 1, 2);
    const auto f = finite_difference(Params{}, 1, 2, 1e-6);
    CHECK(std::abs(a.dw - f.dw) <= 1e-5);
    CHECK(std::abs(a.db - f.db) <= 1e-5);
    CHECK(std::abs(a.dv - f.dv) <= 1e-5);
    CHECK(std::abs(a.dc - f.dc) <= 1e-5);
}

TEST_CASE("gradient check also holds at a non-trivial point") {
    const Params p{-0.7, 0.3, 1.9, -0.4};
    const auto a = gradients(p, 0.8, -1.2);
    const auto f = finite_difference(p, 0.8, -1.2, 1e-6);
    CHECK(std::abs(a.dw - f.dw) <= 1e-5);
    CHECK(std::abs(a.dv - f.dv) <= 1e-5);
}

TEST_CASE("one simultaneous update uses the original gradients") {
    const auto p1 = step(Params{}, gradients(Params{}, 1, 2), 0.1);
    CHECK(p1.w == doctest::Approx(0.62094645).epsilon(1e-8));
    CHECK(p1.b == doctest::Approx(0.12094645).epsilon(1e-8));
    CHECK(p1.v == doctest::Approx(1.07106820).epsilon(1e-8));
    CHECK(p1.c == doctest::Approx(0.15378828).epsilon(1e-8));
    CHECK(forward(p1, 1, 2).loss == doctest::Approx(0.68577377).epsilon(1e-8));
}

TEST_CASE("100 training steps: loss decreases monotonically toward zero") {
    const auto t = train(Params{}, 1, 2, 0.1, 100);
    REQUIRE(t.loss.size() == 101);
    for (std::size_t i = 1; i < t.loss.size(); ++i) CHECK(t.loss[i] < t.loss[i - 1]);
    CHECK(t.loss.back() < 1e-6);
}

} // TEST_SUITE
