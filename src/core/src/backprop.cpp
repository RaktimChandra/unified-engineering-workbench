#include "wb/backprop.hpp"

#include <cmath>

namespace wb::bp {

Forward forward(const Params& p, double x, double y) {
    Forward f;
    f.z = p.w * x + p.b;
    f.h = std::tanh(f.z);
    f.y_hat = p.v * f.h + p.c;
    f.error = f.y_hat - y;
    f.loss = 0.5 * f.error * f.error;
    return f;
}

Gradients gradients(const Params& p, double x, double y) {
    const Forward f = forward(p, x, y);
    const double dJ_dyhat = f.error;            // d(0.5 e^2)/dy_hat
    const double dJ_dh = dJ_dyhat * p.v;        // y_hat = v h + c
    const double dh_dz = 1.0 - f.h * f.h;       // tanh'(z) = 1 - tanh^2(z)
    const double dJ_dz = dJ_dh * dh_dz;
    Gradients g;
    g.dc = dJ_dyhat * 1.0;
    g.dv = dJ_dyhat * f.h;
    g.db = dJ_dz * 1.0;                         // z = w x + b
    g.dw = dJ_dz * x;
    return g;
}

Gradients finite_difference(const Params& p, double x, double y, double eps) {
    auto J = [&](Params q) { return forward(q, x, y).loss; };
    auto central = [&](double Params::*m) {
        Params plus = p, minus = p;
        plus.*m += eps;
        minus.*m -= eps;
        return (J(plus) - J(minus)) / (2.0 * eps);
    };
    return {central(&Params::w), central(&Params::b), central(&Params::v), central(&Params::c)};
}

Params step(const Params& p, const Gradients& g, double lr) {
    return {p.w - lr * g.dw, p.b - lr * g.db, p.v - lr * g.dv, p.c - lr * g.dc};
}

TrainingTrace train(Params p, double x, double y, double lr, int steps) {
    TrainingTrace t;
    for (int i = 0; i <= steps; ++i) {
        t.params.push_back(p);
        t.loss.push_back(forward(p, x, y).loss);
        if (i < steps) p = step(p, gradients(p, x, y), lr);
    }
    return t;
}

} // namespace wb::bp
