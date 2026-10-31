// Steady 1-D heat conduction in a uniform rod with two-node linear elements (Task C).
//
//   -d/dx( k A dT/dx ) = q A   on 0 < x < L,   T(0) = T0,  T(L) = TL
//
// Units: x, L [m]; A [m^2]; k [W/(m K)]; q [W/m^3]; T [deg C]. Because the
// equation is linear in T, Celsius and Kelvin give identical gradients.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace wb::fea {

struct RodProblem {
    double length = 1.0;      ///< L  [m]
    double area = 1e-4;       ///< A  [m^2]
    double conductivity = 10; ///< k  [W/(m K)]
    double heat_gen = 1e4;    ///< q  [W/m^3]
    double t_left = 100;      ///< T(0) [C]
    double t_right = 20;      ///< T(L) [C]

    /// Returns an empty string if valid, otherwise an actionable message.
    std::string validate() const;
};

/// Closed-form solution T(x) = T0 + (TL-T0) x/L + q/(2k) x (L-x).
double exact_temperature(const RodProblem& p, double x);
/// Closed-form heat flux magnitude k dT/dx, used for flux checks.
double exact_gradient(const RodProblem& p, double x);

/// Tridiagonal system in (sub, diag, sup) storage; sub[0] and sup[n-1] unused.
struct Tridiagonal {
    std::vector<double> sub, diag, sup;
    std::size_t size() const noexcept { return diag.size(); }
};

struct Assembly {
    Tridiagonal K;          ///< global conductance matrix before BCs [W/K]
    std::vector<double> F;  ///< global load vector before BCs [W]
};

/// Assembles element contributions ke = (kA/h)[[1,-1],[-1,1]], fe = (qAh/2)[1,1].
Assembly assemble(const RodProblem& p, std::size_t elements);

struct Solution {
    std::size_t elements = 0;
    double h = 0;                     ///< element length [m]
    std::vector<double> x;            ///< node coordinates [m]
    std::vector<double> T;            ///< nodal temperatures [C]
    double reaction_left = 0;         ///< boundary heat flow (K T - F)_0  [W]
    double reaction_right = 0;        ///< boundary heat flow (K T - F)_N  [W]
    double heat_balance_residual = 0; ///< R0 + RN + q A L, should be ~0 [W]
};

/// Solves with exact Dirichlet elimination and the Thomas algorithm.
/// `scratch` (optional) avoids per-call allocation in batch runs.
Solution solve(const RodProblem& p, std::size_t elements);

/// Lightweight batch entry point: writes N+1 nodal temperatures into `out`
/// using caller-owned scratch buffers; no heap allocation once warmed up.
struct Scratch { std::vector<double> c, d; };
void solve_into(const RodProblem& p, std::size_t elements, double* out, Scratch& scratch);

/// Thomas algorithm (no pivoting; valid for the SPD, diagonally dominant K here).
std::vector<double> thomas(const Tridiagonal& A, const std::vector<double>& rhs);
/// Dense Gaussian elimination with partial pivoting, used only as a test oracle.
std::vector<double> dense_gauss(std::vector<std::vector<double>> A, std::vector<double> b);

/// Linear FE interpolation of the nodal solution at an arbitrary x in [0, L].
double interpolate(const Solution& s, double x);

struct ErrorReport {
    std::size_t elements = 0;
    double h = 0;
    double max_nodal_error = 0;     ///< max |T_h - T| at nodes
    double max_midpoint_error = 0;  ///< max |T_h - T| at element midpoints (FE interpolation)
    double max_sampled_error = 0;   ///< max over 64 samples per element
    double l2_error = 0;            ///< sqrt(int (T_h - T)^2 dx), 4-point Gauss per element [C m^0.5]
    double max_temperature = 0;     ///< max nodal T_h
};

ErrorReport evaluate(const RodProblem& p, const Solution& s);

struct ConvergenceRow {
    ErrorReport err;
    double midpoint_rate = 0; ///< log2(e_{N/2}/e_N); 0 for the first row
    double l2_rate = 0;
};

/// Runs the mesh study (default N = 10, 20, 40, 80) and observed rates.
std::vector<ConvergenceRow> convergence_study(const RodProblem& p,
                                              const std::vector<std::size_t>& meshes = {10, 20, 40, 80});

} // namespace wb::fea
