// Deterministic export helpers: CSV tables and self-contained SVG line plots.
// Used by wb_cli to write results/ and by the HTTP layer for downloads, so the
// files a user exports from the UI are byte-identical to the committed ones.
#pragma once

#include "wb/backprop.hpp"
#include "wb/benchmark.hpp"
#include "wb/fea1d.hpp"

#include <string>
#include <vector>

namespace wb::report {

struct Series {
    std::string label;
    std::vector<double> x, y;
    std::string color = "#1f5fbf";
    bool markers = false;
    bool dashed = false;
};

struct PlotSpec {
    std::string title, x_label, y_label;
    bool log_x = false, log_y = false;
    int width = 760, height = 460;
    std::vector<Series> series;
    std::string footnote;
};

std::string svg_line_plot(const PlotSpec& spec);

// --- Task C ---------------------------------------------------------------
/// Per mesh: node,x_m,T_fe_C,T_exact_C,abs_error_C for each node and each midpoint.
std::string fea_profiles_csv(const fea::RodProblem& p, const std::vector<std::size_t>& meshes);
std::string fea_convergence_csv(const std::vector<fea::ConvergenceRow>& rows);
std::string fea_profile_plot(const fea::RodProblem& p, const std::vector<std::size_t>& meshes);
std::string fea_convergence_plot(const std::vector<fea::ConvergenceRow>& rows);

// --- Task D ---------------------------------------------------------------
std::string bench_csv(const bench::Report& r);
std::string bench_text(const bench::Report& r); ///< human-readable host + workload summary
std::string bench_plot(const bench::Report& r);

// --- Task F ---------------------------------------------------------------
std::string backprop_csv(const bp::TrainingTrace& t);
std::string backprop_gradcheck_csv(const bp::Gradients& analytic, const bp::Gradients& fd);
std::string backprop_plot(const bp::TrainingTrace& t);

std::string fmt(double v, int precision = 10); ///< %.{p}g, locale independent

} // namespace wb::report
