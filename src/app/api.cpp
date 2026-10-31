#include "api.hpp"

#include "json_codec.hpp"
#include "wb/backprop.hpp"
#include "wb/benchmark.hpp"
#include "wb/fea1d.hpp"
#include "wb/nodal_csv.hpp"
#include "wb/parallel.hpp"
#include "wb/report.hpp"
#include "wb/stl.hpp"

#include <httplib/httplib.h>

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace wb::app {
namespace fs = std::filesystem;

// ------------------------------------------------------------------ log ----

void LogBuffer::set_sink(Sink s) {
    std::lock_guard<std::mutex> lk(mu_);
    sink_ = std::move(s);
}

void LogBuffer::add(const std::string& level, const std::string& msg) {
    std::lock_guard<std::mutex> lk(mu_);
    const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    lines_.push_back({{"seq", next_++}, {"t", t}, {"level", level}, {"msg", msg}});
    if (lines_.size() > 500) lines_.pop_front();
    if (sink_) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "[%9.3f] %-5s ", t, level.c_str());
        sink_(buf + msg);
    }
}

nlohmann::json LogBuffer::since(std::size_t seq) const {
    std::lock_guard<std::mutex> lk(mu_);
    json a = json::array();
    for (const auto& l : lines_)
        if (l["seq"].get<std::size_t>() > seq) a.push_back(l);
    return a;
}

// --------------------------------------------------------------- helpers ----
namespace {

void send_json(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_header("Cache-Control", "no-store");
    res.set_content(j.dump(), "application/json");
}

void send_error(httplib::Response& res, int status, const std::string& code, const std::string& message,
                const std::string& hint = {}) {
    send_json(res, {{"error", {{"code", code}, {"message", message}, {"hint", hint}}}}, status);
}

json parse_body(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    try {
        return json::parse(req.body);
    } catch (const std::exception&) {
        throw std::invalid_argument("Request body is not valid JSON.");
    }
}

template <typename T>
T get_or(const json& j, const char* key, T fallback) {
    if (!j.contains(key)) return fallback;
    try {
        return j.at(key).get<T>();
    } catch (const std::exception&) {
        throw std::invalid_argument(std::string("Field '") + key + "' has the wrong type.");
    }
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + p.filename().string());
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

void append_floats(std::string& blob, const float* data, std::size_t count) {
    const std::size_t off = blob.size();
    blob.resize(off + count * sizeof(float));
    std::memcpy(&blob[off], data, count * sizeof(float));
}

void append_doubles_as(std::string& blob, const std::vector<double>& v, bool as_float) {
    const std::size_t off = blob.size();
    if (as_float) {
        blob.resize(off + v.size() * sizeof(float));
        for (std::size_t i = 0; i < v.size(); ++i) {
            const float f = static_cast<float>(v[i]);
            std::memcpy(&blob[off + i * sizeof(float)], &f, sizeof f);
        }
    } else {
        blob.resize(off + v.size() * sizeof(double));
        std::memcpy(&blob[off], v.data(), v.size() * sizeof(double));
    }
}

/// Resolves a bundled example by name without allowing path traversal.
fs::path example_path(const fs::path& data_dir, const std::string& name) {
    if (name.empty() || name.find("..") != std::string::npos || name.find(':') != std::string::npos ||
        name.front() == '/' || name.front() == '\\')
        throw std::invalid_argument("Invalid example name.");
    const fs::path p = data_dir / fs::path(name);
    if (!fs::is_regular_file(p)) throw std::invalid_argument("Example '" + name + "' was not found in data/.");
    return p;
}

struct FeaStudyOut {
    json result;
    std::map<std::string, std::string> exports;
};

FeaStudyOut run_fea_study(const fea::RodProblem& p, const std::vector<std::size_t>& meshes) {
    FeaStudyOut out;
    const auto rows = fea::convergence_study(p, meshes);
    json jrows = json::array(), profiles = json::array();
    for (const auto& r : rows)
        jrows.push_back({{"error", to_json(r.err)}, {"midpoint_rate", r.midpoint_rate}, {"l2_rate", r.l2_rate}});
    for (std::size_t n : meshes) {
        const fea::Solution s = fea::solve(p, n);
        json mids = json::array();
        for (std::size_t e = 0; e < n; ++e) {
            const double xm = 0.5 * (s.x[e] + s.x[e + 1]);
            const double th = 0.5 * (s.T[e] + s.T[e + 1]);
            mids.push_back({xm, th, fea::exact_temperature(p, xm)});
        }
        profiles.push_back({{"elements", n},
                            {"x", s.x},
                            {"T", s.T},
                            {"midpoints", mids},
                            {"reaction_left_W", s.reaction_left},
                            {"reaction_right_W", s.reaction_right},
                            {"heat_balance_residual_W", s.heat_balance_residual}});
    }
    json exact = json::array();
    for (int i = 0; i <= 200; ++i) {
        const double x = p.length * i / 200.0;
        exact.push_back({x, fea::exact_temperature(p, x)});
    }
    out.result = {{"problem", to_json(p)}, {"rows", jrows}, {"profiles", profiles}, {"exact", exact}};
    out.exports["fea_profiles.csv"] = report::fea_profiles_csv(p, meshes);
    out.exports["fea_convergence.csv"] = report::fea_convergence_csv(rows);
    out.exports["fea_profiles.svg"] = report::fea_profile_plot(p, meshes);
    out.exports["fea_convergence.svg"] = report::fea_convergence_plot(rows);
    return out;
}

/// The same checks as the unit tests' headline assertions, runnable from the
/// shipped exe so an assessor can verify numerics on their own machine.
json self_test(Job& job, const fs::path& data_dir, hw::DeviceManager& devices) {
    json checks = json::array();
    auto check = [&](const std::string& name, bool pass, const std::string& detail) {
        checks.push_back({{"name", name}, {"pass", pass}, {"detail", detail}});
    };
    const fea::RodProblem p;
    job.set_progress(0.05, "FEA checks");
    {
        const auto rows = fea::convergence_study(p);
        double worst_nodal = 0;
        bool mid_ok = true;
        for (const auto& r : rows) {
            worst_nodal = std::max(worst_nodal, r.err.max_nodal_error);
            mid_ok = mid_ok && std::abs(r.err.max_midpoint_error - 125.0 * r.err.h * r.err.h) < 1e-9;
        }
        check("FEA nodal values exact (linear elements, 1-D)", worst_nodal < 1e-10,
              "max nodal error " + report::fmt(worst_nodal, 3) + " C");
        check("FEA midpoint error = q h^2 / (8k) for every mesh", mid_ok,
              "N=10: " + report::fmt(rows[0].err.max_midpoint_error, 6) + " C (theory 1.25 C)");
        check("FEA observed convergence rate ~ 2", std::abs(rows.back().midpoint_rate - 2.0) < 1e-6,
              "rate 40->80: " + report::fmt(rows.back().midpoint_rate, 8));
        const auto s = fea::solve(p, 40);
        check("FEA energy balance (reactions = q A L)", std::abs(s.heat_balance_residual) < 1e-12,
              "residual " + report::fmt(s.heat_balance_residual, 3) + " W");
        const auto a = fea::assemble(p, 8);
        std::vector<std::vector<double>> dense(9, std::vector<double>(9, 0.0));
        for (std::size_t i = 0; i < 9; ++i) {
            dense[i][i] = a.K.diag[i];
            if (i + 1 < 9) { dense[i][i + 1] = a.K.sup[i]; dense[i + 1][i] = a.K.sub[i + 1]; }
        }
        std::vector<double> F = a.F;
        for (std::size_t c = 0; c < 9; ++c) { dense[0][c] = c == 0; dense[8][c] = c == 8; }
        F[0] = p.t_left;
        F[8] = p.t_right;
        const auto xd = fea::dense_gauss(dense, F);
        const auto xt = fea::solve(p, 8).T;
        double d = 0;
        for (std::size_t i = 0; i < 9; ++i) d = std::max(d, std::abs(xd[i] - xt[i]));
        check("Thomas solver == dense Gaussian elimination", d < 1e-11, "max diff " + report::fmt(d, 3) + " C");
    }
    job.set_progress(0.35, "import checks");
    {
        const auto ok = stl::parse_ascii(read_file(data_dir / "heatsink_fins.stl"));
        check("STL example parses and is closed", ok.ok && ok.mesh.stats.closed,
              std::to_string(ok.mesh.stats.triangles) + " triangles");
        const auto bad = stl::parse_ascii("solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0 nan\n");
        check("STL with NaN is rejected with a line number", !bad.ok && bad.errors[0].code == "stl.non_finite" &&
                                                                 bad.errors[0].line == 4,
              bad.ok ? "accepted!" : bad.errors[0].message);
        const auto csv = nodal::parse(read_file(data_dir / "heatsink_nodal_temperature_SYNTHETIC.csv"));
        check("Nodal CSV example parses", csv.ok, std::to_string(csv.field.size()) + " nodes");
        const auto bad_csv = nodal::parse("node_id,x_m,y_m,z_m,temperature_C\n1,0,0,0,inf\n");
        check("CSV with Inf is rejected", !bad_csv.ok && bad_csv.errors[0].code == "csv.non_finite",
              bad_csv.ok ? "accepted!" : bad_csv.errors[0].message);
    }
    job.set_progress(0.55, "parallel agreement");
    {
        par::BatchSpec spec;
        spec.cases = 200;
        spec.elements = 400;
        const auto cases = par::generate(spec);
        const auto serial = par::run_serial(cases, spec.elements);
        par::WorkerPool pool(std::max(2u, std::thread::hardware_concurrency() > 4 ? 4u : 2u));
        const auto parallel = par::run_parallel(pool, cases, spec.elements);
        const double d = par::max_abs_difference(serial, parallel);
        check("Serial vs parallel batch agreement <= 1e-8 C", d <= 1e-8,
              "max diff " + report::fmt(d, 3) + " C over " + std::to_string(serial.temperatures.size()) + " values");
        check("Batch generator reproducible (fixed seed)",
              par::generate(spec)[123].problem.conductivity == cases[123].problem.conductivity,
              "case 123: k = " + report::fmt(cases[123].problem.conductivity, 12));
    }
    job.set_progress(0.75, "backprop gradient check");
    {
        const bp::Params p0;
        const auto a = bp::gradients(p0, 1, 2);
        const auto f = bp::finite_difference(p0, 1, 2, 1e-6);
        const double e = std::max({std::abs(a.dw - f.dw), std::abs(a.db - f.db), std::abs(a.dv - f.dv),
                                   std::abs(a.dc - f.dc)});
        check("Backprop analytic vs central difference <= 1e-5", e <= 1e-5, "max abs error " + report::fmt(e, 3));
    }
    job.set_progress(0.9, "hardware mock recovery");
    {
        hw::DeviceManager local(std::chrono::milliseconds(0));
        local.inject("jtag0", {1, false});
        hw::RetryPolicy pol;
        pol.sleep = false;
        const auto o = local.connect("jtag0", pol);
        check("[SIMULATED] JTAG timeout recovered by retry", o.final_status.ok() && o.recovered,
              std::to_string(o.attempts.size()) + " attempts");
        (void)devices;
    }
    bool all = true;
    for (const auto& c : checks) all = all && c["pass"].get<bool>();
    return {{"checks", checks}, {"all_pass", all}};
}

} // namespace

// ------------------------------------------------------------------- Api ----

Api::Api(ApiConfig cfg) : cfg_(std::move(cfg)), devices_(cfg_.device_latency) {}

double Api::seconds_since_heartbeat() const {
    const long long now =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count();
    return (now - last_heartbeat_ms_.load()) / 1000.0;
}

void Api::register_routes(httplib::Server& srv) {
    srv.set_payload_max_length(cfg_.max_upload_bytes + 1024);
    if (!cfg_.web_root.empty()) srv.set_mount_point("/", cfg_.web_root.string());
    srv.set_file_request_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Cache-Control", "no-cache");
    });
    srv.set_exception_handler([this](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
        std::string what = "unknown error";
        try { std::rethrow_exception(ep); }
        catch (const std::invalid_argument& e) { log_.add("WARN", req.path + ": " + e.what()); send_error(res, 400, "bad_request", e.what()); return; }
        catch (const std::exception& e) { what = e.what(); }
        catch (...) {}
        log_.add("ERROR", req.path + ": " + what);
        send_error(res, 500, "internal", what, "See logs/workbench.log.");
    });
    srv.set_logger([this](const httplib::Request& req, const httplib::Response& res) {
        if (req.path.rfind("/api/", 0) == 0 && req.path != "/api/heartbeat" && req.path != "/api/log" &&
            req.path.rfind("/api/jobs/", 0) != 0)
            log_.add(res.status >= 400 ? "WARN" : "INFO", req.method + " " + req.path + " -> " + std::to_string(res.status));
    });

    // ---- lifecycle --------------------------------------------------------
    srv.Get("/api/health", [this](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"ok", true}, {"version", cfg_.version}, {"launch_to_ready_ms", cfg_.launch_to_ready_ms}});
    });
    srv.Get("/api/system", [this](const httplib::Request&, httplib::Response& res) {
        send_json(res, {{"host", to_json(sys::query())},
                        {"version", cfg_.version},
                        {"launch_to_ready_ms", cfg_.launch_to_ready_ms},
                        {"limits", {{"max_upload_bytes", cfg_.max_upload_bytes}}}});
    });
    srv.Post("/api/heartbeat", [this](const httplib::Request&, httplib::Response& res) {
        last_heartbeat_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count();
        heartbeat_seen_ = true;
        page_closed_ = false;
        send_json(res, {{"ok", true}});
    });
    srv.Post("/api/bye", [this](const httplib::Request&, httplib::Response& res) {
        page_closed_ = true; // a reload sends a new heartbeat within seconds and clears this
        last_heartbeat_ms_ = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0_).count();
        send_json(res, {{"ok", true}});
    });
    srv.Post("/api/shutdown", [this](const httplib::Request&, httplib::Response& res) {
        log_.add("INFO", "shutdown requested from the UI");
        shutdown_ = true;
        send_json(res, {{"ok", true}});
    });
    srv.Get("/api/log", [this](const httplib::Request& req, httplib::Response& res) {
        const std::size_t since = req.has_param("since") ? std::stoul(req.get_param_value("since")) : 0;
        send_json(res, {{"lines", log_.since(since)}});
    });

    // ---- examples ---------------------------------------------------------
    srv.Get("/api/examples", [this](const httplib::Request&, httplib::Response& res) {
        const fs::path manifest = cfg_.data_dir / "manifest.json";
        if (!fs::exists(manifest)) return send_json(res, {{"examples", json::array()}});
        send_json(res, json::parse(read_file(manifest)));
    });

    // ---- jobs -------------------------------------------------------------
    srv.Get(R"(/api/jobs/([A-Za-z0-9]+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto job = jobs_.get(req.matches[1]);
        if (!job) return send_error(res, 404, "no_job", "Unknown or expired job id.");
        send_json(res, job->status_json());
    });
    srv.Post(R"(/api/jobs/([A-Za-z0-9]+)/cancel)", [this](const httplib::Request& req, httplib::Response& res) {
        if (!jobs_.cancel(req.matches[1])) return send_error(res, 404, "no_job", "Unknown or expired job id.");
        log_.add("INFO", "cancel requested for job " + std::string(req.matches[1]));
        send_json(res, {{"ok", true}});
    });
    srv.Get(R"(/api/jobs/([A-Za-z0-9]+)/blob)", [this](const httplib::Request& req, httplib::Response& res) {
        auto job = jobs_.get(req.matches[1]);
        JobOutput out;
        if (!job || !job->output(out)) return send_error(res, 404, "no_result", "Job has no completed result.");
        res.set_header("Cache-Control", "no-store");
        res.set_content(out.blob, "application/octet-stream");
    });
    srv.Get(R"(/api/jobs/([A-Za-z0-9]+)/export/([A-Za-z0-9_.\-]+))",
            [this](const httplib::Request& req, httplib::Response& res) {
                auto job = jobs_.get(req.matches[1]);
                JobOutput out;
                if (!job || !job->output(out)) return send_error(res, 404, "no_result", "Job has no completed result.");
                const std::string name = req.matches[2];
                auto it = out.exports.find(name);
                if (it == out.exports.end()) return send_error(res, 404, "no_export", "No export named " + name);
                const bool svg = name.size() > 4 && name.substr(name.size() - 4) == ".svg";
                const bool csv = name.size() > 4 && name.substr(name.size() - 4) == ".csv";
                res.set_header("Content-Disposition", "attachment; filename=\"" + name + "\"");
                res.set_content(it->second, svg ? "image/svg+xml" : csv ? "text/csv" : "text/plain");
            });

    // ---- Task B: geometry and nodal results -------------------------------
    auto import_source = [this](const httplib::Request& req, std::string& name) {
        if (req.has_param("example")) {
            name = req.get_param_value("example");
            return read_file(example_path(cfg_.data_dir, name));
        }
        name = req.has_param("name") ? req.get_param_value("name") : "upload";
        return req.body;
    };

    srv.Post("/api/geometry/import", [this, import_source](const httplib::Request& req, httplib::Response& res) {
        std::string name;
        auto text = std::make_shared<std::string>(import_source(req, name));
        const std::string units = req.has_param("units") ? req.get_param_value("units") : "mm";
        if (units != "mm" && units != "m" && units != "in")
            throw std::invalid_argument("units must be 'mm', 'm' or 'in' (STL files carry no unit).");
        log_.add("INFO", "geometry import: " + name + " (" + std::to_string(text->size()) + " bytes)");
        auto job = jobs_.submit("geometry", [this, text, name, units](Job& j) {
            stl::Limits lim;
            lim.max_bytes = cfg_.max_upload_bytes;
            const auto r = stl::parse_ascii(*text, lim, [&](double f, const std::string& s) { j.set_progress(f, s); },
                                            &j.cancel_token());
            if (!r.ok) {
                log_.add("WARN", name + ": " + r.errors[0].message);
                throw JobFailure{{{"code", r.errors[0].code},
                                  {"message", r.errors[0].message},
                                  {"hint", r.errors[0].hint},
                                  {"line", r.errors[0].line},
                                  {"file", name}}};
            }
            JobOutput out;
            out.result = {{"file", name},
                          {"units", units},
                          {"solid_name", r.mesh.solid_name},
                          {"bytes", text->size()},
                          {"stats", to_json(r.mesh.stats)},
                          {"warnings", to_json(r.warnings)},
                          {"layout", {{"positions_f32", r.mesh.positions.size()}, {"normals_f32", r.mesh.normals.size()}}}};
            append_floats(out.blob, r.mesh.positions.data(), r.mesh.positions.size());
            append_floats(out.blob, r.mesh.normals.data(), r.mesh.normals.size());
            log_.add("INFO", name + ": " + std::to_string(r.mesh.stats.triangles) + " triangles, " +
                                 (r.mesh.stats.closed ? "closed" : "open"));
            return out;
        });
        send_json(res, {{"job_id", job->id()}}, 202);
    });

    srv.Post("/api/results/import", [this, import_source](const httplib::Request& req, httplib::Response& res) {
        std::string name;
        auto text = std::make_shared<std::string>(import_source(req, name));
        log_.add("INFO", "nodal CSV import: " + name + " (" + std::to_string(text->size()) + " bytes)");
        auto job = jobs_.submit("nodal", [this, text, name](Job& j) {
            nodal::Limits lim;
            lim.max_bytes = cfg_.max_upload_bytes;
            auto r = nodal::parse(*text, lim, [&](double f, const std::string& s) { j.set_progress(f, s); },
                                  &j.cancel_token());
            if (!r.ok) {
                log_.add("WARN", name + ": " + r.errors[0].message);
                throw JobFailure{{{"code", r.errors[0].code},
                                  {"message", r.errors[0].message},
                                  {"hint", r.errors[0].hint},
                                  {"line", r.errors[0].line},
                                  {"file", name}}};
            }
            auto field = std::make_shared<nodal::NodalField>(std::move(r.field));
            JobOutput out;
            out.result = {{"file", name},
                          {"nodes", field->size()},
                          {"t_min_C", field->t_min},
                          {"t_max_C", field->t_max},
                          {"t_mean_C", field->t_mean},
                          {"bounds_m", {{"min", field->bmin}, {"max", field->bmax}}},
                          {"warnings", to_json(r.warnings)},
                          {"layout", "f32 xyz[3n] | f32 T[n] | f64 node_id[n]"}};
            std::vector<double> xyz;
            xyz.reserve(field->size() * 3);
            for (std::size_t i = 0; i < field->size(); ++i) {
                xyz.push_back(field->x[i]); xyz.push_back(field->y[i]); xyz.push_back(field->z[i]);
            }
            append_doubles_as(out.blob, xyz, true);
            append_doubles_as(out.blob, field->temperature_c, true);
            std::vector<double> ids(field->node_id.begin(), field->node_id.end());
            append_doubles_as(out.blob, ids, false);
            out.retained = field;
            log_.add("INFO", name + ": " + std::to_string(field->size()) + " nodes, T in [" +
                                 report::fmt(field->t_min, 5) + ", " + report::fmt(field->t_max, 5) + "] C");
            return out;
        });
        send_json(res, {{"job_id", job->id()}}, 202);
    });

    // Exact (double precision) node inspection served by the C++ model.
    srv.Get(R"(/api/results/([A-Za-z0-9]+)/node/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        auto job = jobs_.get(req.matches[1]);
        auto field = job ? std::static_pointer_cast<const nodal::NodalField>(job->retained()) : nullptr;
        if (!field) return send_error(res, 404, "no_result", "No nodal result for this id.");
        const std::size_t i = std::stoul(req.matches[2]);
        if (i >= field->size()) return send_error(res, 404, "bad_index", "Node index out of range.");
        send_json(res, {{"index", i},
                        {"node_id", field->node_id[i]},
                        {"x_m", field->x[i]},
                        {"y_m", field->y[i]},
                        {"z_m", field->z[i]},
                        {"temperature_C", field->temperature_c[i]}});
    });

    // ---- Task C: thermal FEA ----------------------------------------------
    srv.Post("/api/fea/solve", [](const httplib::Request& req, httplib::Response& res) {
        const json body = parse_body(req);
        const fea::RodProblem p = rod_from_json(body.value("problem", json::object()));
        const std::string why = p.validate();
        if (!why.empty()) return send_error(res, 400, "invalid_problem", why);
        const auto ne = get_or<std::size_t>(body, "elements", 10);
        if (ne < 1 || ne > 1'000'000)
            return send_error(res, 400, "invalid_mesh", "Number of elements must be between 1 and 1,000,000.");
        const fea::Solution s = fea::solve(p, ne);
        json j{{"x", s.x}, {"T", s.T}, {"error", to_json(fea::evaluate(p, s))},
               {"reaction_left_W", s.reaction_left}, {"reaction_right_W", s.reaction_right},
               {"heat_balance_residual_W", s.heat_balance_residual}};
        if (ne <= 12) { // show the assembled system so the assembly is inspectable
            const auto a = fea::assemble(p, ne);
            j["assembly"] = {{"sub", a.K.sub}, {"diag", a.K.diag}, {"sup", a.K.sup}, {"F", a.F}};
        }
        send_json(res, j);
    });

    srv.Post("/api/fea/study", [this](const httplib::Request& req, httplib::Response& res) {
        const json body = parse_body(req);
        const fea::RodProblem p = rod_from_json(body.value("problem", json::object()));
        const std::string why = p.validate();
        if (!why.empty()) return send_error(res, 400, "invalid_problem", why);
        auto meshes = get_or<std::vector<std::size_t>>(body, "meshes", {10, 20, 40, 80});
        if (meshes.empty() || meshes.size() > 12)
            return send_error(res, 400, "invalid_mesh", "Provide between 1 and 12 mesh sizes.");
        for (auto n : meshes)
            if (n < 1 || n > 200000) return send_error(res, 400, "invalid_mesh", "Each mesh must have 1..200000 elements.");
        auto job = jobs_.submit("fea_study", [p, meshes](Job& j) {
            j.set_progress(0.2, "solving " + std::to_string(meshes.size()) + " meshes");
            FeaStudyOut s = run_fea_study(p, meshes);
            JobOutput out;
            out.result = std::move(s.result);
            out.exports = std::move(s.exports);
            return out;
        });
        send_json(res, {{"job_id", job->id()}}, 202);
    });

    // ---- Task D: multicore batch ------------------------------------------
    srv.Post("/api/bench/run", [this](const httplib::Request& req, httplib::Response& res) {
        const json body = parse_body(req);
        bench::Config cfg;
        cfg.batch.seed = get_or<std::uint64_t>(body, "seed", cfg.batch.seed);
        cfg.batch.cases = get_or<std::size_t>(body, "cases", cfg.batch.cases);
        cfg.batch.elements = get_or<std::size_t>(body, "elements", cfg.batch.elements);
        cfg.workers = get_or<std::vector<std::size_t>>(body, "workers", cfg.workers);
        cfg.warmup_runs = get_or<int>(body, "warmup_runs", 1);
        cfg.measured_runs = get_or<int>(body, "measured_runs", 5);
        if (cfg.batch.cases < 1 || cfg.batch.cases > 200000)
            return send_error(res, 400, "invalid", "Case count must be between 1 and 200,000.");
        if (cfg.batch.elements < 1 || cfg.batch.elements > 100000)
            return send_error(res, 400, "invalid", "Mesh size must be between 1 and 100,000 elements.");
        if (cfg.batch.cases * (cfg.batch.elements + 1) > 60'000'000)
            return send_error(res, 400, "too_large", "cases x nodes exceeds 60 million values (~480 MB per result).",
                               "Reduce the case count or the mesh size.");
        if (cfg.measured_runs < 1 || cfg.measured_runs > 50 || cfg.warmup_runs < 0 || cfg.warmup_runs > 10)
            return send_error(res, 400, "invalid", "Use 1-50 measured runs and 0-10 warm-up runs.");
        log_.add("INFO", "benchmark: " + std::to_string(cfg.batch.cases) + " cases x " +
                             std::to_string(cfg.batch.elements) + " elements");
        auto job = jobs_.submit("benchmark", [this, cfg](Job& j) {
            const auto rep = bench::run(cfg, [&](double f, const std::string& s) { j.set_progress(f, s); },
                                        &j.cancel_token());
            JobOutput out;
            out.result = to_json(rep);
            out.exports["benchmark.csv"] = report::bench_csv(rep);
            out.exports["benchmark_host.txt"] = report::bench_text(rep);
            out.exports["benchmark_speedup.svg"] = report::bench_plot(rep);
            log_.add("INFO", std::string("benchmark finished; agreement ") + (rep.agreement_ok ? "PASS" : "FAIL"));
            return out;
        });
        send_json(res, {{"job_id", job->id()}}, 202);
    });

    // ---- Task E: hardware (simulated) ---------------------------------------
    srv.Get("/api/hw/devices", [this](const httplib::Request&, httplib::Response& res) {
        json a = json::array();
        for (const auto& d : devices_.discover())
            a.push_back({{"info", to_json(d)},
                         {"state", hw::to_string(devices_.state(d.id))},
                         {"capabilities", to_json(devices_.capabilities(d.id))}});
        send_json(res, {{"devices", a}, {"simulated", true}});
    });
    srv.Post(R"(/api/hw/([a-z0-9]+)/connect)", [this](const httplib::Request& req, httplib::Response& res) {
        const std::string id = req.matches[1];
        auto job = jobs_.submit("hw_connect", [this, id](Job& j) {
            j.set_progress(0.1, "connecting " + id);
            const auto o = devices_.connect(id);
            for (const auto& a : o.attempts)
                log_.add(a.status.ok() ? "INFO" : "WARN",
                         id + " attempt " + std::to_string(a.attempt) + ": " +
                             (a.status.ok() ? std::string("connected") : a.status.message) +
                             (a.backoff_ms ? " -> retry in " + std::to_string(a.backoff_ms) + " ms" : ""));
            if (o.recovered) log_.add("INFO", id + " recovered after " + std::to_string(o.attempts.size()) + " attempts");
            JobOutput out;
            out.result = to_json(o);
            return out;
        });
        send_json(res, {{"job_id", job->id()}}, 202);
    });
    srv.Post(R"(/api/hw/([a-z0-9]+)/disconnect)", [this](const httplib::Request& req, httplib::Response& res) {
        const auto s = devices_.disconnect(req.matches[1]);
        log_.add("INFO", std::string(req.matches[1]) + " disconnected");
        send_json(res, to_json(s), s.ok() ? 200 : 404);
    });
    srv.Get(R"(/api/hw/([a-z0-9]+)/telemetry)", [this](const httplib::Request& req, httplib::Response& res) {
        hw::TelemetryMessage m;
        const auto s = devices_.poll(req.matches[1], m);
        if (!s.ok()) {
            if (s.code == hw::ErrorCode::link_lost) log_.add("WARN", s.message);
            return send_json(res, {{"status", to_json(s)}, {"state", hw::to_string(devices_.state(req.matches[1]))}}, 409);
        }
        send_json(res, {{"status", to_json(s)}, {"message", to_json(m)}});
    });
    srv.Post(R"(/api/hw/([a-z0-9]+)/fault)", [this](const httplib::Request& req, httplib::Response& res) {
        const json body = parse_body(req);
        hw::FaultPlan plan;
        plan.connect_timeouts = get_or<int>(body, "connect_timeouts", 0);
        plan.drop_link_on_next_poll = get_or<bool>(body, "drop_link", false);
        if (plan.connect_timeouts < 0 || plan.connect_timeouts > 5)
            return send_error(res, 400, "invalid", "connect_timeouts must be 0..5.");
        devices_.inject(req.matches[1], plan);
        log_.add("WARN", "[SIMULATED] fault armed on " + std::string(req.matches[1]) + ": " +
                             std::to_string(plan.connect_timeouts) + " timeout(s)" +
                             (plan.drop_link_on_next_poll ? ", link drop on next poll" : ""));
        send_json(res, {{"ok", true}});
    });

    // ---- Task F: backprop ---------------------------------------------------
    srv.Post("/api/backprop/run", [this](const httplib::Request& req, httplib::Response& res) {
        const json body = parse_body(req);
        const int steps = get_or<int>(body, "steps", 100);
        const double lr = get_or<double>(body, "learning_rate", 0.1);
        if (steps < 1 || steps > 100000) return send_error(res, 400, "invalid", "steps must be 1..100000.");
        if (!(lr > 0 && lr < 10 && std::isfinite(lr))) return send_error(res, 400, "invalid", "learning rate must be in (0, 10).");
        auto job = jobs_.submit("backprop", [steps, lr](Job&) {
            const bp::Params p0;
            const double x = 1, y = 2;
            const auto fw = bp::forward(p0, x, y);
            const auto g = bp::gradients(p0, x, y);
            const auto fd = bp::finite_difference(p0, x, y, 1e-6);
            const auto p1 = bp::step(p0, g, lr);
            const auto trace = bp::train(p0, x, y, lr, steps);
            JobOutput out;
            out.result = {{"initial", to_json(p0)},
                          {"forward", {{"z", fw.z}, {"h", fw.h}, {"y_hat", fw.y_hat}, {"error", fw.error}, {"loss", fw.loss}}},
                          {"gradients", to_json(g)},
                          {"finite_difference", to_json(fd)},
                          {"updated", to_json(p1)},
                          {"updated_loss", bp::forward(p1, x, y).loss},
                          {"loss", trace.loss},
                          {"learning_rate", lr}};
            out.exports["backprop_loss.csv"] = report::backprop_csv(trace);
            out.exports["backprop_gradcheck.csv"] = report::backprop_gradcheck_csv(g, fd);
            out.exports["backprop_loss.svg"] = report::backprop_plot(trace);
            return out;
        });
        send_json(res, {{"job_id", job->id()}}, 202);
    });

    // ---- self verification ----------------------------------------------------
    srv.Post("/api/selftest", [this](const httplib::Request&, httplib::Response& res) {
        auto job = jobs_.submit("selftest", [this](Job& j) {
            JobOutput out;
            out.result = self_test(j, cfg_.data_dir, devices_);
            log_.add(out.result["all_pass"].get<bool>() ? "INFO" : "ERROR",
                     std::string("self-verification ") + (out.result["all_pass"].get<bool>() ? "passed" : "FAILED"));
            return out;
        });
        send_json(res, {{"job_id", job->id()}}, 202);
    });
}

} // namespace wb::app
