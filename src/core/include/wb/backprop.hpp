// Hand-derived backpropagation for a 1-1-1 tanh network (Task F).
//   h = tanh(w x + b),  y_hat = v h + c,  J = 0.5 (y_hat - y)^2
// No automatic differentiation: every derivative below is written out by hand.
#pragma once

#include <array>
#include <vector>

namespace wb::bp {

struct Params {
    double w = 0.5, b = 0.0, v = 1.0, c = 0.0;
};

struct Forward {
    double z = 0, h = 0, y_hat = 0, error = 0, loss = 0;
};

struct Gradients {
    double dw = 0, db = 0, dv = 0, dc = 0;
};

Forward forward(const Params& p, double x, double y);
/// Analytic chain-rule gradients:
///   dJ/dc = e,  dJ/dv = e h,  dJ/db = e v (1 - h^2),  dJ/dw = e v (1 - h^2) x,  with e = y_hat - y.
Gradients gradients(const Params& p, double x, double y);
/// Central finite differences (J(theta+eps) - J(theta-eps)) / (2 eps) for each parameter.
Gradients finite_difference(const Params& p, double x, double y, double eps = 1e-6);
/// One simultaneous gradient-descent step using gradients of the *original* parameters.
Params step(const Params& p, const Gradients& g, double learning_rate);

struct TrainingTrace {
    std::vector<double> loss;          ///< loss[i] = J before update i+1; loss[0] = initial
    std::vector<Params> params;        ///< params[i] used to compute loss[i]
};
TrainingTrace train(Params p, double x, double y, double learning_rate, int steps);

} // namespace wb::bp
