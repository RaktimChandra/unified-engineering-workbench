#include "wb/fea1d.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wb::fea {

std::string RodProblem::validate() const {
    auto bad = [](double v) { return !std::isfinite(v); };
    if (bad(length) || length <= 0) return "Length L must be a finite value > 0 m.";
    if (bad(area) || area <= 0) return "Cross-section area A must be a finite value > 0 m^2.";
    if (bad(conductivity) || conductivity <= 0) return "Conductivity k must be a finite value > 0 W/(m K).";
    if (bad(heat_gen)) return "Heat generation q must be finite (W/m^3); negative values model a sink.";
    if (bad(t_left) || bad(t_right)) return "Boundary temperatures must be finite (deg C).";
    if (t_left < -273.15 || t_right < -273.15) return "Boundary temperatures must be above absolute zero (-273.15 C).";
    return {};
}

double exact_temperature(const RodProblem& p, double x) {
    const double L = p.length;
    return p.t_left + (p.t_right - p.t_left) * x / L + p.heat_gen / (2.0 * p.conductivity) * x * (L - x);
}

double exact_gradient(const RodProblem& p, double x) {
    const double L = p.length;
    return (p.t_right - p.t_left) / L + p.heat_gen / (2.0 * p.conductivity) * (L - 2.0 * x);
}

Assembly assemble(const RodProblem& p, std::size_t ne) {
    if (ne < 1) throw std::invalid_argument("at least one element is required");
    const std::size_t nn = ne + 1;
    const double h = p.length / static_cast<double>(ne);
    const double ke = p.conductivity * p.area / h;    // W/K
    const double fe = 0.5 * p.heat_gen * p.area * h;  // W per node (exact for constant q)
    Assembly a;
    a.K.sub.assign(nn, 0.0);
    a.K.diag.assign(nn, 0.0);
    a.K.sup.assign(nn, 0.0);
    a.F.assign(nn, 0.0);
    for (std::size_t e = 0; e < ne; ++e) {
        const std::size_t i = e, j = e + 1; // global node numbers of element e
        a.K.diag[i] += ke;
        a.K.diag[j] += ke;
        a.K.sup[i] -= ke;  // K(i, j)
        a.K.sub[j] -= ke;  // K(j, i)
        a.F[i] += fe;
        a.F[j] += fe;
    }
    return a;
}

std::vector<double> thomas(const Tridiagonal& A, const std::vector<double>& rhs) {
    const std::size_t n = A.size();
    std::vector<double> c(n), d(n), x(n);
    c[0] = A.sup[0] / A.diag[0];
    d[0] = rhs[0] / A.diag[0];
    for (std::size_t i = 1; i < n; ++i) {
        const double m = A.diag[i] - A.sub[i] * c[i - 1];
        c[i] = (i + 1 < n) ? A.sup[i] / m : 0.0;
        d[i] = (rhs[i] - A.sub[i] * d[i - 1]) / m;
    }
    x[n - 1] = d[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) x[i] = d[i] - c[i] * x[i + 1];
    return x;
}

std::vector<double> dense_gauss(std::vector<std::vector<double>> A, std::vector<double> b) {
    const std::size_t n = b.size();
    for (std::size_t k = 0; k < n; ++k) {
        std::size_t piv = k;
        for (std::size_t r = k + 1; r < n; ++r)
            if (std::abs(A[r][k]) > std::abs(A[piv][k])) piv = r;
        if (A[piv][k] == 0.0) throw std::runtime_error("singular matrix");
        std::swap(A[k], A[piv]);
        std::swap(b[k], b[piv]);
        for (std::size_t r = k + 1; r < n; ++r) {
            const double f = A[r][k] / A[k][k];
            for (std::size_t c = k; c < n; ++c) A[r][c] -= f * A[k][c];
            b[r] -= f * b[k];
        }
    }
    std::vector<double> x(n);
    for (std::size_t k = n; k-- > 0;) {
        double s = b[k];
        for (std::size_t c = k + 1; c < n; ++c) s -= A[k][c] * x[c];
        x[k] = s / A[k][k];
    }
    return x;
}

namespace {

/// Imposes T_0 and T_N by symmetric elimination: known values move to the RHS
/// of the neighbouring equations, and the constrained rows become identities.
/// Keeping K symmetric matters: a row-only replacement also gives the right
/// answer here but breaks symmetry, which a Cholesky/CG solver would reject.
void apply_dirichlet(Tridiagonal& K, std::vector<double>& F, double t0, double tn) {
    const std::size_t n = K.size();
    F[1] -= K.sub[1] * t0;
    K.sub[1] = 0.0;
    F[n - 2] -= K.sup[n - 2] * tn;
    K.sup[n - 2] = 0.0;
    K.diag[0] = 1.0; K.sup[0] = 0.0; F[0] = t0;
    K.diag[n - 1] = 1.0; K.sub[n - 1] = 0.0; F[n - 1] = tn;
}

} // namespace

Solution solve(const RodProblem& p, std::size_t ne) {
    const std::string why = p.validate();
    if (!why.empty()) throw std::invalid_argument(why);
    Assembly a = assemble(p, ne);
    const Tridiagonal K_full = a.K;
    const std::vector<double> F_full = a.F;
    apply_dirichlet(a.K, a.F, p.t_left, p.t_right);

    Solution s;
    s.elements = ne;
    s.h = p.length / static_cast<double>(ne);
    s.T = thomas(a.K, a.F);
    s.x.resize(ne + 1);
    for (std::size_t i = 0; i <= ne; ++i) s.x[i] = p.length * static_cast<double>(i) / static_cast<double>(ne);
    s.x[ne] = p.length;

    // Reactions R = K_full T - F_full at the constrained nodes: the heat the
    // prescribed temperature must supply there (negative = heat leaves the rod).
    const std::size_t n = ne + 1;
    s.reaction_left = K_full.diag[0] * s.T[0] + K_full.sup[0] * s.T[1] - F_full[0];
    s.reaction_right = K_full.sub[n - 1] * s.T[n - 2] + K_full.diag[n - 1] * s.T[n - 1] - F_full[n - 1];
    // Steady state: everything generated inside (q A L) leaves through the ends,
    // so the two inflows sum to -q A L and this residual must vanish.
    s.heat_balance_residual = s.reaction_left + s.reaction_right + p.heat_gen * p.area * p.length;
    return s;
}

void solve_into(const RodProblem& p, std::size_t ne, double* out, Scratch& sc) {
    // Same discretisation as assemble()+apply_dirichlet()+thomas(), fused so a
    // batch of thousands of solves allocates nothing after warm-up. The unit
    // tests assert bitwise equality with solve() for representative inputs.
    const std::size_t n = ne + 1;
    const double h = p.length / static_cast<double>(ne);
    const double ke = p.conductivity * p.area / h;
    const double fe = 0.5 * p.heat_gen * p.area * h;
    if (sc.c.size() < n) { sc.c.resize(n); sc.d.resize(n); }
    double* c = sc.c.data();
    double* d = sc.d.data();
    // Row 0: identity (Dirichlet).
    c[0] = 0.0;
    d[0] = p.t_left;
    if (n == 2) { out[0] = p.t_left; out[1] = p.t_right; return; }
    for (std::size_t i = 1; i < n; ++i) {
        double sub = -ke, diag = 2.0 * ke, sup = -ke, rhs = 2.0 * fe;
        if (i == n - 1) { sub = 0.0; diag = 1.0; sup = 0.0; rhs = p.t_right; }
        else {
            if (i == 1) { rhs -= (-ke) * p.t_left; sub = 0.0; }
            if (i == n - 2) { rhs -= (-ke) * p.t_right; sup = 0.0; }
        }
        const double m = diag - sub * c[i - 1];
        c[i] = (i + 1 < n) ? sup / m : 0.0;
        d[i] = (rhs - sub * d[i - 1]) / m;
    }
    out[n - 1] = d[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) out[i] = d[i] - c[i] * out[i + 1];
}

double interpolate(const Solution& s, double x) {
    const std::size_t ne = s.elements;
    const double L = s.x.back();
    if (x <= 0) return s.T.front();
    if (x >= L) return s.T.back();
    std::size_t e = std::min<std::size_t>(static_cast<std::size_t>(x / s.h), ne - 1);
    const double xi = (x - s.x[e]) / (s.x[e + 1] - s.x[e]); // local coordinate in [0,1]
    return (1.0 - xi) * s.T[e] + xi * s.T[e + 1];           // N1 = 1-xi, N2 = xi
}

ErrorReport evaluate(const RodProblem& p, const Solution& s) {
    ErrorReport r;
    r.elements = s.elements;
    r.h = s.h;
    r.max_temperature = *std::max_element(s.T.begin(), s.T.end());
    for (std::size_t i = 0; i < s.x.size(); ++i)
        r.max_nodal_error = std::max(r.max_nodal_error, std::abs(s.T[i] - exact_temperature(p, s.x[i])));
    // 4-point Gauss-Legendre on [-1,1]; integrates the degree-4 squared error exactly.
    static const double gx[4] = {-0.8611363115940526, -0.3399810435848563, 0.3399810435848563, 0.8611363115940526};
    static const double gw[4] = {0.3478548451374538, 0.6521451548625461, 0.6521451548625461, 0.3478548451374538};
    double l2 = 0;
    for (std::size_t e = 0; e < s.elements; ++e) {
        const double xa = s.x[e], xb = s.x[e + 1];
        const double xm = 0.5 * (xa + xb);
        const double tm = 0.5 * (s.T[e] + s.T[e + 1]); // FE interpolant at the midpoint
        r.max_midpoint_error = std::max(r.max_midpoint_error, std::abs(tm - exact_temperature(p, xm)));
        for (int k = 0; k <= 64; ++k) {
            const double xi = k / 64.0;
            const double x = xa + xi * (xb - xa);
            const double th = (1 - xi) * s.T[e] + xi * s.T[e + 1];
            r.max_sampled_error = std::max(r.max_sampled_error, std::abs(th - exact_temperature(p, x)));
        }
        for (int g = 0; g < 4; ++g) {
            const double xi = 0.5 * (gx[g] + 1.0);
            const double x = xa + xi * (xb - xa);
            const double th = (1 - xi) * s.T[e] + xi * s.T[e + 1];
            const double diff = th - exact_temperature(p, x);
            l2 += 0.5 * (xb - xa) * gw[g] * diff * diff;
        }
    }
    r.l2_error = std::sqrt(l2);
    return r;
}

std::vector<ConvergenceRow> convergence_study(const RodProblem& p, const std::vector<std::size_t>& meshes) {
    std::vector<ConvergenceRow> rows;
    for (std::size_t i = 0; i < meshes.size(); ++i) {
        ConvergenceRow row;
        row.err = evaluate(p, solve(p, meshes[i]));
        if (i > 0) {
            const auto& prev = rows.back().err;
            const double ratio = static_cast<double>(meshes[i]) / static_cast<double>(meshes[i - 1]);
            row.midpoint_rate = std::log(prev.max_midpoint_error / row.err.max_midpoint_error) / std::log(ratio);
            row.l2_rate = std::log(prev.l2_error / row.err.l2_error) / std::log(ratio);
        }
        rows.push_back(row);
    }
    return rows;
}

} // namespace wb::fea
