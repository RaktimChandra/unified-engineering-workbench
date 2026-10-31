#include "wb/report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace wb::report {

std::string fmt(double v, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*g", precision, v); // programs start in the "C" locale
    return buf;
}

namespace {

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '&': o += "&amp;"; break;
        case '<': o += "&lt;"; break;
        case '>': o += "&gt;"; break;
        case '"': o += "&quot;"; break;
        default: o += c;
        }
    }
    return o;
}

/// "Nice" linear ticks: step is 1, 2 or 5 x 10^k.
std::vector<double> nice_ticks(double lo, double hi, int target = 6) {
    if (!(hi > lo)) { hi = lo + 1; }
    const double raw = (hi - lo) / target;
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double r = raw / mag;
    const double step = (r < 1.5 ? 1 : r < 3.5 ? 2 : r < 7.5 ? 5 : 10) * mag;
    std::vector<double> t;
    for (double v = std::ceil(lo / step - 1e-9) * step; v <= hi + step * 1e-9; v += step)
        t.push_back(std::abs(v) < step * 1e-9 ? 0.0 : v);
    return t;
}

std::string tick_label(double v, bool log) {
    if (log) {
        const int e = static_cast<int>(std::lround(std::log10(v)));
        if (e >= -2 && e <= 3) return fmt(v, 3);
        return "1e" + std::to_string(e);
    }
    return fmt(v, 6);
}

} // namespace

std::string svg_line_plot(const PlotSpec& s) {
    const double ml = 78, mr = 24, mt = 44, mb = s.footnote.empty() ? 58 : 76;
    const double pw = s.width - ml - mr, ph = s.height - mt - mb;
    double xmin = INFINITY, xmax = -INFINITY, ymin = INFINITY, ymax = -INFINITY;
    for (const auto& se : s.series)
        for (std::size_t i = 0; i < se.x.size(); ++i) {
            const double x = se.x[i], y = se.y[i];
            if ((s.log_x && x <= 0) || (s.log_y && y <= 0) || !std::isfinite(x) || !std::isfinite(y)) continue;
            xmin = std::min(xmin, x); xmax = std::max(xmax, x);
            ymin = std::min(ymin, y); ymax = std::max(ymax, y);
        }
    if (!std::isfinite(xmin)) { xmin = 0; xmax = 1; ymin = 0; ymax = 1; }
    auto tx = [&](double v) { return s.log_x ? std::log10(v) : v; };
    auto ty = [&](double v) { return s.log_y ? std::log10(v) : v; };
    double ax0 = tx(xmin), ax1 = tx(xmax), ay0 = ty(ymin), ay1 = ty(ymax);
    if (s.log_x) { ax0 = std::floor(ax0); ax1 = std::ceil(ax1); }
    if (s.log_y) { ay0 = std::floor(ay0); ay1 = std::ceil(ay1); }
    if (!s.log_y) { const double pad = 0.05 * (ay1 - ay0 > 0 ? ay1 - ay0 : 1); ay0 -= pad; ay1 += pad; }
    if (ax1 <= ax0) ax1 = ax0 + 1;
    if (ay1 <= ay0) ay1 = ay0 + 1;
    auto px = [&](double v) { return ml + (tx(v) - ax0) / (ax1 - ax0) * pw; };
    auto py = [&](double v) { return mt + ph - (ty(v) - ay0) / (ay1 - ay0) * ph; };

    std::ostringstream o;
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << s.width << "\" height=\"" << s.height
      << "\" viewBox=\"0 0 " << s.width << ' ' << s.height << "\" font-family=\"Helvetica, Arial, sans-serif\">\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    o << "<text x=\"" << ml << "\" y=\"26\" font-size=\"16\" font-weight=\"600\" fill=\"#1b1f24\">" << esc(s.title)
      << "</text>\n";

    auto ticks_for = [](double a0, double a1, bool log) {
        std::vector<double> t;
        if (log) for (int e = static_cast<int>(a0); e <= static_cast<int>(a1); ++e) t.push_back(std::pow(10.0, e));
        else t = nice_ticks(a0, a1);
        return t;
    };
    for (double v : ticks_for(ax0, ax1, s.log_x)) {
        if (tx(v) < ax0 - 1e-9 || tx(v) > ax1 + 1e-9) continue;
        const double X = px(v);
        o << "<line x1=\"" << X << "\" y1=\"" << mt << "\" x2=\"" << X << "\" y2=\"" << mt + ph
          << "\" stroke=\"#e6e8eb\"/>\n<text x=\"" << X << "\" y=\"" << mt + ph + 18
          << "\" font-size=\"11\" text-anchor=\"middle\" fill=\"#444\">" << tick_label(v, s.log_x) << "</text>\n";
    }
    for (double v : ticks_for(ay0, ay1, s.log_y)) {
        if (ty(v) < ay0 - 1e-9 || ty(v) > ay1 + 1e-9) continue;
        const double Y = py(v);
        o << "<line x1=\"" << ml << "\" y1=\"" << Y << "\" x2=\"" << ml + pw << "\" y2=\"" << Y
          << "\" stroke=\"#e6e8eb\"/>\n<text x=\"" << ml - 8 << "\" y=\"" << Y + 4
          << "\" font-size=\"11\" text-anchor=\"end\" fill=\"#444\">" << tick_label(v, s.log_y) << "</text>\n";
    }
    o << "<rect x=\"" << ml << "\" y=\"" << mt << "\" width=\"" << pw << "\" height=\"" << ph
      << "\" fill=\"none\" stroke=\"#8a9099\"/>\n";
    o << "<text x=\"" << ml + pw / 2 << "\" y=\"" << mt + ph + 40
      << "\" font-size=\"13\" text-anchor=\"middle\" fill=\"#1b1f24\">" << esc(s.x_label) << "</text>\n";
    o << "<text transform=\"translate(18," << mt + ph / 2
      << ") rotate(-90)\" font-size=\"13\" text-anchor=\"middle\" fill=\"#1b1f24\">" << esc(s.y_label) << "</text>\n";

    for (const auto& se : s.series) {
        std::ostringstream pts;
        for (std::size_t i = 0; i < se.x.size(); ++i) {
            if ((s.log_x && se.x[i] <= 0) || (s.log_y && se.y[i] <= 0)) continue;
            pts << fmt(px(se.x[i]), 7) << ',' << fmt(py(se.y[i]), 7) << ' ';
        }
        o << "<polyline fill=\"none\" stroke=\"" << se.color << "\" stroke-width=\"1.8\""
          << (se.dashed ? " stroke-dasharray=\"6 4\"" : "") << " points=\"" << pts.str() << "\"/>\n";
        if (se.markers)
            for (std::size_t i = 0; i < se.x.size(); ++i) {
                if ((s.log_x && se.x[i] <= 0) || (s.log_y && se.y[i] <= 0)) continue;
                o << "<circle cx=\"" << fmt(px(se.x[i]), 7) << "\" cy=\"" << fmt(py(se.y[i]), 7)
                  << "\" r=\"3.2\" fill=\"#fff\" stroke=\"" << se.color << "\" stroke-width=\"1.5\"/>\n";
            }
    }
    // Legend (top-right inside the plot frame).
    const double lw = 250, lh = 18.0 * s.series.size() + 10;
    const double lx = ml + pw - lw - 8, ly = mt + 8;
    o << "<rect x=\"" << lx << "\" y=\"" << ly << "\" width=\"" << lw << "\" height=\"" << lh
      << "\" fill=\"#ffffff\" fill-opacity=\"0.92\" stroke=\"#c9ced6\"/>\n";
    for (std::size_t i = 0; i < s.series.size(); ++i) {
        const double y = ly + 15 + 18.0 * i;
        o << "<line x1=\"" << lx + 8 << "\" y1=\"" << y - 4 << "\" x2=\"" << lx + 32 << "\" y2=\"" << y - 4
          << "\" stroke=\"" << s.series[i].color << "\" stroke-width=\"2\""
          << (s.series[i].dashed ? " stroke-dasharray=\"6 4\"" : "") << "/>\n<text x=\"" << lx + 40 << "\" y=\"" << y
          << "\" font-size=\"11.5\" fill=\"#1b1f24\">" << esc(s.series[i].label) << "</text>\n";
    }
    if (!s.footnote.empty())
        o << "<text x=\"" << ml << "\" y=\"" << s.height - 12 << "\" font-size=\"10.5\" fill=\"#5b616b\">"
          << esc(s.footnote) << "</text>\n";
    o << "</svg>\n";
    return o.str();
}

// ------------------------------------------------------------------ Task C --

static const char* kPalette[] = {"#d1495b", "#edae49", "#00798c", "#30638e", "#6a4c93", "#2b9348"};

std::string fea_profiles_csv(const fea::RodProblem& p, const std::vector<std::size_t>& meshes) {
    std::ostringstream o;
    o << "elements,kind,index,x_m,T_fe_C,T_exact_C,abs_error_C\n";
    for (std::size_t n : meshes) {
        const fea::Solution s = fea::solve(p, n);
        for (std::size_t i = 0; i < s.x.size(); ++i) {
            const double te = fea::exact_temperature(p, s.x[i]);
            o << n << ",node," << i << ',' << fmt(s.x[i], 12) << ',' << fmt(s.T[i], 15) << ',' << fmt(te, 15) << ','
              << fmt(std::abs(s.T[i] - te), 6) << '\n';
        }
        for (std::size_t e = 0; e < n; ++e) {
            const double xm = 0.5 * (s.x[e] + s.x[e + 1]);
            const double th = 0.5 * (s.T[e] + s.T[e + 1]);
            const double te = fea::exact_temperature(p, xm);
            o << n << ",midpoint," << e << ',' << fmt(xm, 12) << ',' << fmt(th, 15) << ',' << fmt(te, 15) << ','
              << fmt(std::abs(th - te), 6) << '\n';
        }
    }
    return o.str();
}

std::string fea_convergence_csv(const std::vector<fea::ConvergenceRow>& rows) {
    std::ostringstream o;
    o << "elements,h_m,max_nodal_error_C,max_midpoint_error_C,max_sampled_error_C,l2_error_C_m05,"
         "midpoint_rate,l2_rate,max_T_C\n";
    for (const auto& r : rows)
        o << r.err.elements << ',' << fmt(r.err.h, 12) << ',' << fmt(r.err.max_nodal_error, 6) << ','
          << fmt(r.err.max_midpoint_error, 12) << ',' << fmt(r.err.max_sampled_error, 12) << ','
          << fmt(r.err.l2_error, 12) << ',' << fmt(r.midpoint_rate, 6) << ',' << fmt(r.l2_rate, 6) << ','
          << fmt(r.err.max_temperature, 12) << '\n';
    return o.str();
}

std::string fea_profile_plot(const fea::RodProblem& p, const std::vector<std::size_t>& meshes) {
    PlotSpec spec;
    spec.title = "Task C: temperature along the rod, FE (N elements) vs exact";
    spec.x_label = "x  [m]";
    spec.y_label = "Temperature  T  [°C]";
    Series ex;
    ex.label = "Exact  T(x) = 100 - 80x + 500x(1-x)";
    ex.color = "#111111";
    ex.dashed = true;
    for (int i = 0; i <= 400; ++i) {
        const double x = p.length * i / 400.0;
        ex.x.push_back(x);
        ex.y.push_back(fea::exact_temperature(p, x));
    }
    for (std::size_t m = 0; m < meshes.size(); ++m) {
        const fea::Solution s = fea::solve(p, meshes[m]);
        Series se;
        se.label = "FE, N = " + std::to_string(meshes[m]) + " (linear between nodes)";
        se.color = kPalette[m % 6];
        se.markers = meshes[m] <= 20;
        se.x = s.x;
        se.y = s.T;
        spec.series.push_back(se);
    }
    spec.series.push_back(ex);
    spec.footnote = "L = 1 m, A = 1e-4 m^2, k = 10 W/(m K), q = 1e4 W/m^3, T(0) = 100 C, T(L) = 20 C.";
    return svg_line_plot(spec);
}

std::string fea_convergence_plot(const std::vector<fea::ConvergenceRow>& rows) {
    PlotSpec spec;
    spec.title = "Task C: mesh convergence (log-log)";
    spec.x_label = "Element size  h  [m]";
    spec.y_label = "Error  [°C]  (L2: °C·m^0.5)";
    spec.log_x = spec.log_y = true;
    Series mid{"max |error| at element midpoints", {}, {}, "#d1495b", true};
    Series l2{"L2 error norm", {}, {}, "#00798c", true};
    Series ref{"reference slope h^2 (125 h^2)", {}, {}, "#777777", false, true};
    for (const auto& r : rows) {
        mid.x.push_back(r.err.h); mid.y.push_back(r.err.max_midpoint_error);
        l2.x.push_back(r.err.h); l2.y.push_back(r.err.l2_error);
        ref.x.push_back(r.err.h); ref.y.push_back(125.0 * r.err.h * r.err.h);
    }
    spec.series = {mid, l2, ref};
    spec.footnote = "Nodal error is at round-off level (~1e-13 C) and cannot be shown on this axis; see CSV.";
    return svg_line_plot(spec);
}

// ------------------------------------------------------------------ Task D --

std::string bench_csv(const bench::Report& r) {
    std::ostringstream o;
    o << "variant,workers,median_s,speedup,efficiency,max_abs_diff_C";
    for (std::size_t i = 0; i < r.rows.front().seconds.size(); ++i) o << ",run" << i + 1 << "_s";
    o << '\n';
    for (const auto& row : r.rows) {
        o << row.variant << ',' << row.workers << ',' << fmt(row.median_s, 8) << ',' << fmt(row.speedup, 6) << ','
          << fmt(row.efficiency, 6) << ',' << fmt(row.max_abs_diff_c, 6);
        for (double s : row.seconds) o << ',' << fmt(s, 8);
        o << '\n';
    }
    return o.str();
}

std::string bench_text(const bench::Report& r) {
    std::ostringstream o;
    const auto& h = r.host;
    o << "CPU model         : " << h.cpu_model << '\n'
      << "Logical CPUs      : " << h.logical_cpus << '\n'
      << "Physical cores    : " << (h.physical_cores ? std::to_string(h.physical_cores) : "not exposed by OS") << '\n'
      << "RAM               : " << fmt(static_cast<double>(h.ram_bytes) / (1024.0 * 1024 * 1024), 4) << " GiB\n"
      << "OS                : " << h.os << '\n'
      << "Compiler          : " << h.compiler << '\n'
      << "Build type / flags: " << h.build_type << " / " << h.build_flags << '\n'
      << "Workload          : " << r.config.batch.cases << " independent rod solves x " << r.config.batch.elements
      << " linear elements (" << r.config.batch.elements + 1 << " nodes), seed " << r.config.batch.seed << '\n'
      << "                    k ~ U[" << r.config.batch.k_min << ", " << r.config.batch.k_max << "] W/(m K), q ~ U["
      << r.config.batch.q_min << ", " << r.config.batch.q_max << "] W/m^3\n"
      << "Repetitions       : " << r.config.warmup_runs << " warm-up (discarded) + " << r.config.measured_runs
      << " measured, median reported\n"
      << "Timed region      : solve loop only (case generation " << fmt(r.generate_s * 1e3, 4)
      << " ms, thread creation and comparisons excluded)\n"
      << "Agreement         : max |T_serial - T_parallel| <= " << r.tolerance_c << " C -> "
      << (r.agreement_ok ? "PASS" : "FAIL") << '\n';
    if (!r.skipped_workers.empty()) {
        o << "Skipped workers   :";
        for (auto w : r.skipped_workers) o << ' ' << w;
        o << " (exceed logical CPU count)\n";
    }
    if (!r.note.empty()) o << "Note              : " << r.note << '\n';
    return o.str();
}

std::string bench_plot(const bench::Report& r) {
    PlotSpec spec;
    spec.title = "Task D: measured speedup T1/Tp vs worker count";
    spec.x_label = "Workers  p";
    spec.y_label = "Speedup  T_serial / T_p  [-]";
    Series meas{"measured (median of " + std::to_string(r.config.measured_runs) + ")", {}, {}, "#30638e", true};
    Series ideal{"ideal linear speedup", {}, {}, "#999999", false, true};
    for (const auto& row : r.rows)
        if (row.variant == "pool") {
            meas.x.push_back(static_cast<double>(row.workers));
            meas.y.push_back(row.speedup);
            ideal.x.push_back(static_cast<double>(row.workers));
            ideal.y.push_back(static_cast<double>(row.workers));
        }
    spec.series = {meas, ideal};
    spec.footnote = r.host.cpu_model + ", " + std::to_string(r.host.logical_cpus) + " logical CPUs, " + r.host.compiler;
    return svg_line_plot(spec);
}

// ------------------------------------------------------------------ Task F --

std::string backprop_csv(const bp::TrainingTrace& t) {
    std::ostringstream o;
    o << "step,w,b,v,c,loss\n";
    for (std::size_t i = 0; i < t.loss.size(); ++i) {
        const auto& p = t.params[i];
        o << i << ',' << fmt(p.w, 12) << ',' << fmt(p.b, 12) << ',' << fmt(p.v, 12) << ',' << fmt(p.c, 12) << ','
          << fmt(t.loss[i], 12) << '\n';
    }
    return o.str();
}

std::string backprop_gradcheck_csv(const bp::Gradients& a, const bp::Gradients& f) {
    std::ostringstream o;
    o << "parameter,analytic,central_difference,abs_error,pass_1e-5\n";
    const std::pair<const char*, std::pair<double, double>> rows[] = {
        {"w", {a.dw, f.dw}}, {"b", {a.db, f.db}}, {"v", {a.dv, f.dv}}, {"c", {a.dc, f.dc}}};
    for (const auto& [name, v] : rows) {
        const double err = std::abs(v.first - v.second);
        o << name << ',' << fmt(v.first, 12) << ',' << fmt(v.second, 12) << ',' << fmt(err, 4) << ','
          << (err <= 1e-5 ? "yes" : "NO") << '\n';
    }
    return o.str();
}

std::string backprop_plot(const bp::TrainingTrace& t) {
    PlotSpec spec;
    spec.title = "Task F: training loss, single sample (x=1, y=2), learning rate 0.1";
    spec.x_label = "Gradient-descent step";
    spec.y_label = "Loss  J = 0.5 (y_hat - y)^2  [-]";
    spec.log_y = true;
    Series s{"J after each step (step 0 = initial parameters)", {}, {}, "#6a4c93", false};
    for (std::size_t i = 0; i < t.loss.size(); ++i) {
        s.x.push_back(static_cast<double>(i));
        s.y.push_back(t.loss[i]);
    }
    spec.series = {s};
    spec.footnote = "Log scale. J(0) = " + fmt(t.loss.front(), 6) + ", J(1) = " + fmt(t.loss[1], 6) +
                    ", J(100) = " + fmt(t.loss.back(), 4);
    return svg_line_plot(spec);
}

} // namespace wb::report
