// wb_cli: headless front door to wb_core. Regenerates every artefact in data/
// and results/ so evidence is reproducible without the GUI.
//
//   wb_cli gen-examples <data_dir>
//   wb_cli fea          <out_dir>
//   wb_cli backprop     <out_dir>
//   wb_cli bench        <out_dir> [--cases N] [--elements N] [--workers 1,2,4] [--runs 5] [--warmup 1]
//   wb_cli limits       <scratch_dir>        (file-size limit timing study)
//   wb_cli parse-stl    <file>
//   wb_cli parse-csv    <file>
//   wb_cli sysinfo

#include "wb/backprop.hpp"
#include "wb/benchmark.hpp"
#include "wb/fea1d.hpp"
#include "wb/nodal_csv.hpp"
#include "wb/report.hpp"
#include "wb/stl.hpp"
#include "wb/sysinfo.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using wb::report::fmt;

namespace {

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << content;
    if (!f) throw std::runtime_error("cannot write " + p.string());
    std::cout << "  wrote " << p.generic_string() << " (" << content.size() << " bytes)\n";
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot read " + p.string());
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

// ---------------------------------------------------------------- examples --

struct P2 { double x, y; };

/// Closed, 2-manifold "finned heat sink": a comb-shaped profile extruded along z.
/// Every cap vertex is also a wall vertex, so no T-junctions exist and the
/// edge-manifold check in the parser reports the mesh as closed.
std::string heatsink_stl(double depth, std::size_t& triangles_out) {
    const int fins = 6;
    const double width = 60, base_h = 6, fin_h = 30, fin_w = 2, pitch = 10, first = 4;
    std::vector<double> xs = {0};
    for (int i = 0; i < fins; ++i) { xs.push_back(first + i * pitch); xs.push_back(first + i * pitch + fin_w); }
    xs.push_back(width);

    // Counter-clockwise outline (viewed from +z).
    std::vector<P2> poly;
    for (double x : xs) poly.push_back({x, 0});
    poly.push_back({width, base_h});
    for (int i = fins - 1; i >= 0; --i) {
        const double xa = first + i * pitch, xb = xa + fin_w;
        poly.push_back({xb, base_h});
        poly.push_back({xb, base_h + fin_h});
        poly.push_back({xa, base_h + fin_h});
        poly.push_back({xa, base_h});
    }
    poly.push_back({0, base_h});

    std::ostringstream o;
    std::size_t nt = 0;
    auto tri = [&](const double* a, const double* b, const double* c) {
        const double u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        double n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        for (double& k : n) k /= l;
        char buf[256];
        std::snprintf(buf, sizeof buf, "  facet normal %.6e %.6e %.6e\n    outer loop\n", n[0], n[1], n[2]);
        o << buf;
        for (const double* p : {a, b, c}) {
            std::snprintf(buf, sizeof buf, "      vertex %.6f %.6f %.6f\n", p[0], p[1], p[2]);
            o << buf;
        }
        o << "    endloop\n  endfacet\n";
        ++nt;
    };
    o << "solid finned_heatsink_mm\n";
    // Side walls.
    for (std::size_t i = 0; i < poly.size(); ++i) {
        const P2 p = poly[i], q = poly[(i + 1) % poly.size()];
        const double a[3] = {p.x, p.y, 0}, b[3] = {q.x, q.y, 0}, c[3] = {q.x, q.y, depth}, d[3] = {p.x, p.y, depth};
        tri(a, b, c);
        tri(a, c, d);
    }
    // Caps: base split into columns at every breakpoint, plus one rectangle per fin.
    auto rect = [&](double xa, double ya, double xb, double yb) {
        for (double z : {0.0, depth}) {
            const double A[3] = {xa, ya, z}, B[3] = {xb, ya, z}, C[3] = {xb, yb, z}, D[3] = {xa, yb, z};
            if (z > 0) { tri(A, B, C); tri(A, C, D); }  // +z facing
            else { tri(A, C, B); tri(A, D, C); }        // -z facing
        }
    };
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) rect(xs[i], 0, xs[i + 1], base_h);
    for (int i = 0; i < fins; ++i) rect(first + i * pitch, base_h, first + i * pitch + fin_w, base_h + fin_h);
    o << "endsolid finned_heatsink_mm\n";
    triangles_out = nt;
    return o.str();
}

/// SYNTHETIC nodal temperature field on the same heat-sink volume (metres).
/// Base: conduction from a 85 C heated bottom face; fins: classic adiabatic-tip
/// fin profile T = Ta + (Tb-Ta) cosh(m(Lf-s))/cosh(m Lf). Not an ANSYS export.
std::string heatsink_csv(std::size_t& nodes_out) {
    const double mm = 1e-3, Ta = 25.0, Tbottom = 85.0, m = 45.0, Lf = 30 * mm;
    std::ostringstream o;
    o << "node_id,x_m,y_m,z_m,temperature_C\n";
    std::size_t id = 0;
    char buf[160];
    auto emit = [&](double x, double y, double z, double T) {
        std::snprintf(buf, sizeof buf, "%zu,%.6f,%.6f,%.6f,%.4f\n", ++id, x, y, z, T);
        o << buf;
    };
    for (int iz = 0; iz <= 20; ++iz) {
        const double z = iz * 2 * mm;
        const double airflow = 1.0 - 0.06 * (z / (40 * mm)); // downstream air is warmer -> slightly less cooling
        for (int ix = 0; ix <= 30; ++ix) {
            const double x = ix * 2 * mm;
            const double centre = 1.0 - 0.04 * std::pow((x - 30 * mm) / (30 * mm), 2);
            for (int iy = 0; iy <= 3; ++iy) {
                const double y = iy * 2 * mm;
                const double Tb = Tbottom - 2.5 * (y / (6 * mm));
                emit(x, y, z, Ta + (Tb - Ta) * centre * (1.0 - 0.02 * (1 - airflow)));
            }
        }
        for (int f = 0; f < 6; ++f) {
            for (int ix = 0; ix <= 2; ++ix) {
                const double x = (4 + 10 * f + ix) * mm;
                const double centre = 1.0 - 0.04 * std::pow((x - 30 * mm) / (30 * mm), 2);
                const double Tb = (Tbottom - 2.5) * centre;
                for (int iy = 1; iy <= 15; ++iy) {
                    const double s = iy * 2 * mm;
                    const double ratio = std::cosh(m * (Lf - s)) / std::cosh(m * Lf);
                    emit(x, 6 * mm + s, z, Ta + (Tb - Ta) * ratio * (1.0 - 0.03 * (1 - airflow)));
                }
            }
        }
    }
    nodes_out = id;
    return o.str();
}

std::string binary_stl_one_triangle() {
    std::string b(84 + 50, '\0');
    std::memcpy(&b[0], "binary STL example created by wb_cli", 36);
    const std::uint32_t n = 1;
    std::memcpy(&b[80], &n, 4);
    const float v[12] = {0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::memcpy(&b[84], v, sizeof v);
    return b;
}

int gen_examples(const fs::path& dir) {
    std::size_t nt = 0, nn = 0;
    write_file(dir / "heatsink_fins.stl", heatsink_stl(40.0, nt));
    write_file(dir / "heatsink_nodal_temperature_SYNTHETIC.csv", heatsink_csv(nn));
    write_file(dir / "plate_open.stl",
               "solid open_plate\n"
               "facet normal 0 0 1\n outer loop\n  vertex 0 0 0\n  vertex 10 0 0\n  vertex 10 10 0\n endloop\nendfacet\n"
               "facet normal 0 0 1\n outer loop\n  vertex 0 0 0\n  vertex 10 10 0\n  vertex 0 10 0\n endloop\nendfacet\n"
               "endsolid open_plate\n");
    const fs::path bad = dir / "malformed";
    write_file(bad / "stl_empty.stl", "");
    write_file(bad / "stl_nan_vertex.stl",
               "solid bad\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 nan\nvertex 0 1 0\nendloop\nendfacet\nendsolid bad\n");
    write_file(bad / "stl_truncated.stl", "solid cut\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\n");
    write_file(bad / "stl_four_vertices.stl",
               "solid quad\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 1 1 0\nvertex 0 1 0\n"
               "endloop\nendfacet\nendsolid quad\n");
    write_file(bad / "stl_decimal_comma.stl",
               "solid c\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1,5 0 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid c\n");
    write_file(bad / "stl_no_facets.stl", "solid nothing\nendsolid nothing\n");
    write_file(bad / "stl_binary.stl", binary_stl_one_triangle());
    const std::string hdr = "node_id,x_m,y_m,z_m,temperature_C\n";
    write_file(bad / "csv_empty.csv", "");
    write_file(bad / "csv_header_only.csv", hdr);
    write_file(bad / "csv_bad_header.csv", "id,x,y,z,T\n1,0,0,0,20\n");
    write_file(bad / "csv_nan_temperature.csv", hdr + "1,0,0,0,20.5\n2,0.01,0,0,NaN\n");
    write_file(bad / "csv_duplicate_node.csv", hdr + "1,0,0,0,20\n2,0.01,0,0,21\n1,0.02,0,0,22\n");
    write_file(bad / "csv_semicolon.csv", "node_id;x_m;y_m;z_m;temperature_C\n1;0;0;0;20,5\n");
    write_file(bad / "csv_wrong_column_count.csv", hdr + "1,0,0,0,20\n2,0.01,0,21\n");
    write_file(bad / "csv_below_absolute_zero.csv", hdr + "1,0,0,0,-300\n");

    // Manifest consumed by the UI's example picker (GET /api/examples).
    std::ostringstream m;
    struct E { const char* name; const char* kind; bool valid; const char* desc; };
    const E ex[] = {
        {"heatsink_fins.stl", "stl", true, "Finned heat sink, 6 fins, closed 2-manifold. Units: millimetres."},
        {"plate_open.stl", "stl", true, "Two-triangle open plate (valid, triggers the open-mesh warning)."},
        {"heatsink_nodal_temperature_SYNTHETIC.csv", "csv", true,
         "SYNTHETIC nodal temperatures on the heat-sink volume (analytic fin model, not an ANSYS export). SI metres, C."},
        {"malformed/stl_empty.stl", "stl", false, "0-byte file"},
        {"malformed/stl_nan_vertex.stl", "stl", false, "NaN coordinate on line 5"},
        {"malformed/stl_truncated.stl", "stl", false, "File ends inside a facet"},
        {"malformed/stl_four_vertices.stl", "stl", false, "Quad instead of triangle"},
        {"malformed/stl_decimal_comma.stl", "stl", false, "Decimal comma (regional settings)"},
        {"malformed/stl_no_facets.stl", "stl", false, "Valid solid with no facets"},
        {"malformed/stl_binary.stl", "stl", false, "Binary STL (unsupported by design)"},
        {"malformed/csv_empty.csv", "csv", false, "0-byte file"},
        {"malformed/csv_header_only.csv", "csv", false, "Header but no rows"},
        {"malformed/csv_bad_header.csv", "csv", false, "Wrong column names"},
        {"malformed/csv_nan_temperature.csv", "csv", false, "NaN temperature"},
        {"malformed/csv_duplicate_node.csv", "csv", false, "Repeated node_id"},
        {"malformed/csv_semicolon.csv", "csv", false, "Semicolon-delimited export"},
        {"malformed/csv_wrong_column_count.csv", "csv", false, "Missing value in a row"},
        {"malformed/csv_below_absolute_zero.csv", "csv", false, "Temperature below -273.15 C"},
    };
    m << "{\n  \"generated_by\": \"wb_cli gen-examples (self-created data, CC0-1.0)\",\n  \"examples\": [\n";
    for (std::size_t i = 0; i < std::size(ex); ++i) {
        const bool stl = std::strcmp(ex[i].kind, "stl") == 0;
        m << "    {\"name\": \"" << ex[i].name << "\", \"kind\": \"" << ex[i].kind << "\", \"valid\": "
          << (ex[i].valid ? "true" : "false") << ", \"units\": \"" << (stl ? "mm (STL is unitless; chosen on import)" : "m, deg C")
          << "\", \"provenance\": \"self-created by wb_cli gen-examples\", \"description\": \"" << ex[i].desc << "\"}"
          << (i + 1 < std::size(ex) ? "," : "") << "\n";
    }
    m << "  ]\n}\n";
    write_file(dir / "manifest.json", m.str());
    std::cout << "heatsink: " << nt << " triangles; synthetic field: " << nn << " nodes\n";
    return 0;
}

// --------------------------------------------------------------- results ----

int fea_results(const fs::path& out) {
    const wb::fea::RodProblem p;
    const std::vector<std::size_t> meshes = {10, 20, 40, 80};
    const auto rows = wb::fea::convergence_study(p, meshes);
    write_file(out / "fea_profiles.csv", wb::report::fea_profiles_csv(p, meshes));
    write_file(out / "fea_convergence.csv", wb::report::fea_convergence_csv(rows));
    write_file(out / "fea_profiles.svg", wb::report::fea_profile_plot(p, meshes));
    write_file(out / "fea_convergence.svg", wb::report::fea_convergence_plot(rows));
    std::printf("\n%6s %10s %14s %16s %12s %8s\n", "N", "h [m]", "nodal err [C]", "midpoint err [C]", "L2 err", "rate");
    for (const auto& r : rows)
        std::printf("%6zu %10.5f %14.3e %16.10f %12.6e %8.5f\n", r.err.elements, r.err.h, r.err.max_nodal_error,
                    r.err.max_midpoint_error, r.err.l2_error, r.midpoint_rate);
    const auto s = wb::fea::solve(p, 80);
    std::printf("heat balance (N=80): R0 = %.12f W, RN = %.12f W, q A L = %.6f W, residual = %.3e W\n",
                s.reaction_left, s.reaction_right, p.heat_gen * p.area * p.length, s.heat_balance_residual);
    return 0;
}

int backprop_results(const fs::path& out) {
    const wb::bp::Params p0;
    const auto f = wb::bp::forward(p0, 1, 2);
    const auto g = wb::bp::gradients(p0, 1, 2);
    const auto fd = wb::bp::finite_difference(p0, 1, 2, 1e-6);
    const auto p1 = wb::bp::step(p0, g, 0.1);
    const auto tr = wb::bp::train(p0, 1, 2, 0.1, 100);
    write_file(out / "backprop_gradcheck.csv", wb::report::backprop_gradcheck_csv(g, fd));
    write_file(out / "backprop_loss.csv", wb::report::backprop_csv(tr));
    write_file(out / "backprop_loss.svg", wb::report::backprop_plot(tr));
    std::printf("forward: z=%.8f h=%.8f y_hat=%.8f e=%.8f J=%.8f\n", f.z, f.h, f.y_hat, f.error, f.loss);
    std::printf("grads  : dw=%.8f db=%.8f dv=%.8f dc=%.8f\n", g.dw, g.db, g.dv, g.dc);
    std::printf("fd     : dw=%.8f db=%.8f dv=%.8f dc=%.8f\n", fd.dw, fd.db, fd.dv, fd.dc);
    std::printf("update : w=%.8f b=%.8f v=%.8f c=%.8f  J_new=%.8f\n", p1.w, p1.b, p1.v, p1.c,
                wb::bp::forward(p1, 1, 2).loss);
    std::printf("loss   : J(0)=%.6f J(1)=%.6f J(10)=%.6e J(100)=%.6e\n", tr.loss[0], tr.loss[1], tr.loss[10], tr.loss[100]);
    return 0;
}

std::vector<std::size_t> parse_list(const std::string& s) {
    std::vector<std::size_t> v;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) v.push_back(std::stoul(tok));
    return v;
}

int bench_results(const fs::path& out, int argc, char** argv) {
    wb::bench::Config cfg;
    for (int i = 3; i + 1 < argc; i += 2) {
        const std::string k = argv[i], v = argv[i + 1];
        if (k == "--cases") cfg.batch.cases = std::stoul(v);
        else if (k == "--elements") cfg.batch.elements = std::stoul(v);
        else if (k == "--workers") cfg.workers = parse_list(v);
        else if (k == "--runs") cfg.measured_runs = std::stoi(v);
        else if (k == "--warmup") cfg.warmup_runs = std::stoi(v);
        else if (k == "--seed") cfg.batch.seed = std::stoull(v);
    }
    const auto rep = wb::bench::run(cfg, [](double f, const std::string& s) {
        std::fprintf(stderr, "\r  %5.1f%%  %-28s", 100 * f, s.c_str());
    });
    std::fprintf(stderr, "\n");
    write_file(out / "benchmark.csv", wb::report::bench_csv(rep));
    write_file(out / "benchmark_host.txt", wb::report::bench_text(rep));
    write_file(out / "benchmark_speedup.svg", wb::report::bench_plot(rep));
    std::cout << '\n' << wb::report::bench_text(rep) << '\n' << wb::report::bench_csv(rep);
    return rep.agreement_ok ? 0 : 1;
}

/// Times parsing of generated files of increasing size; used for docs/file_limits.md.
int limits_study(const fs::path& dir) {
    fs::create_directories(dir);
    std::printf("%-6s %12s %12s %12s %10s\n", "kind", "items", "MiB", "parse [s]", "ok");
    for (std::size_t n : {1'000ull, 100'000ull, 1'000'000ull, 2'500'000ull}) {
        std::ostringstream o;
        o << "solid big\n";
        char buf[200];
        for (std::size_t i = 0; i < n; ++i) {
            const double x = static_cast<double>(i % 1000), y = static_cast<double>(i / 1000);
            std::snprintf(buf, sizeof buf,
                          "facet normal 0 0 1\n outer loop\n  vertex %.4f %.4f 0\n  vertex %.4f %.4f 0\n  vertex %.4f %.4f 0\n endloop\nendfacet\n",
                          x, y, x + 1, y, x, y + 1);
            o << buf;
        }
        o << "endsolid big\n";
        const std::string s = o.str();
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = wb::stl::parse_ascii(s);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-6s %12zu %12.1f %12.3f %10s\n", "stl", n, s.size() / 1048576.0, dt, r.ok ? "yes" : r.errors[0].code.c_str());
    }
    for (std::size_t n : {1'000ull, 100'000ull, 1'000'000ull, 4'000'000ull}) {
        std::ostringstream o;
        o << "node_id,x_m,y_m,z_m,temperature_C\n";
        char buf[160];
        for (std::size_t i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof buf, "%zu,%.6f,%.6f,%.6f,%.4f\n", i + 1, (i % 100) * 1e-3, ((i / 100) % 100) * 1e-3,
                          (i / 10000) * 1e-3, 20.0 + static_cast<double>(i % 977) * 0.01);
            o << buf;
        }
        const std::string s = o.str();
        const auto t0 = std::chrono::steady_clock::now();
        const auto r = wb::nodal::parse(s);
        const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%-6s %12zu %12.1f %12.3f %10s\n", "csv", n, s.size() / 1048576.0, dt, r.ok ? "yes" : r.errors[0].code.c_str());
    }
    return 0;
}

void print_diags(const std::vector<wb::Diagnostic>& v, const char* label) {
    for (const auto& d : v)
        std::cout << label << " [" << d.code << "]" << (d.line ? " line " + std::to_string(d.line) : "") << ": "
                  << d.message << (d.hint.empty() ? "" : "\n    hint: " + d.hint) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: wb_cli gen-examples|fea|backprop|bench|limits|parse-stl|parse-csv|sysinfo [args]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "gen-examples" && argc >= 3) return gen_examples(argv[2]);
        if (cmd == "fea" && argc >= 3) return fea_results(argv[2]);
        if (cmd == "backprop" && argc >= 3) return backprop_results(argv[2]);
        if (cmd == "bench" && argc >= 3) return bench_results(argv[2], argc, argv);
        if (cmd == "limits" && argc >= 3) return limits_study(argv[2]);
        if (cmd == "parse-stl" && argc >= 3) {
            const auto r = wb::stl::parse_ascii(read_file(argv[2]));
            print_diags(r.errors, "ERROR");
            print_diags(r.warnings, "warning");
            if (r.ok)
                std::cout << "OK: " << r.mesh.stats.triangles << " triangles, " << r.mesh.stats.unique_vertices
                          << " vertices, closed=" << r.mesh.stats.closed << ", area=" << fmt(r.mesh.stats.surface_area)
                          << ", volume=" << fmt(r.mesh.stats.volume) << '\n';
            return r.ok ? 0 : 1;
        }
        if (cmd == "parse-csv" && argc >= 3) {
            const auto r = wb::nodal::parse(read_file(argv[2]));
            print_diags(r.errors, "ERROR");
            print_diags(r.warnings, "warning");
            if (r.ok)
                std::cout << "OK: " << r.field.size() << " nodes, T in [" << fmt(r.field.t_min) << ", " << fmt(r.field.t_max)
                          << "] C\n";
            return r.ok ? 0 : 1;
        }
        if (cmd == "sysinfo") {
            const auto h = wb::sys::query();
            std::cout << h.cpu_model << " | logical " << h.logical_cpus << " | physical " << h.physical_cores << " | RAM "
                      << h.ram_bytes << " | " << h.os << " | " << h.compiler << " | " << h.build_type << ' '
                      << h.build_flags << '\n';
            return 0;
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    std::cerr << "unknown command or missing argument: " << cmd << '\n';
    return 2;
}
