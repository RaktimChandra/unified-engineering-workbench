#include "json_codec.hpp"

#include <cmath>
#include <stdexcept>

namespace wb::app {

json to_json(const Diagnostic& d) {
    json j{{"code", d.code}, {"message", d.message}, {"hint", d.hint}};
    if (d.line > 0) j["line"] = d.line;
    return j;
}

json to_json(const std::vector<Diagnostic>& v) {
    json a = json::array();
    for (const auto& d : v) a.push_back(to_json(d));
    return a;
}

json to_json(const stl::MeshStats& s) {
    return {{"triangles", s.triangles},
            {"unique_vertices", s.unique_vertices},
            {"degenerate", s.degenerate},
            {"normal_mismatch", s.normal_mismatch},
            {"zero_normals", s.zero_normals},
            {"boundary_edges", s.boundary_edges},
            {"non_manifold_edges", s.non_manifold_edges},
            {"closed", s.closed},
            {"surface_area", s.surface_area},
            {"volume", s.volume},
            {"bounds", {{"min", s.bounds.min}, {"max", s.bounds.max}}}};
}

json to_json(const sys::HostInfo& h) {
    return {{"cpu_model", h.cpu_model},     {"logical_cpus", h.logical_cpus}, {"physical_cores", h.physical_cores},
            {"ram_bytes", h.ram_bytes},     {"os", h.os},                     {"under_wine", h.under_wine},
            {"compiler", h.compiler},       {"build_flags", h.build_flags},   {"build_type", h.build_type}};
}

json to_json(const bench::Report& r) {
    json rows = json::array();
    for (const auto& row : r.rows)
        rows.push_back({{"variant", row.variant},
                        {"workers", row.workers},
                        {"seconds", row.seconds},
                        {"median_s", row.median_s},
                        {"speedup", row.speedup},
                        {"efficiency", row.efficiency},
                        {"max_abs_diff_c", row.max_abs_diff_c}});
    const auto& b = r.config.batch;
    return {{"host", to_json(r.host)},
            {"config",
             {{"seed", b.seed},
              {"cases", b.cases},
              {"elements", b.elements},
              {"k_range", {b.k_min, b.k_max}},
              {"q_range", {b.q_min, b.q_max}},
              {"workers", r.config.workers},
              {"warmup_runs", r.config.warmup_runs},
              {"measured_runs", r.config.measured_runs}}},
            {"skipped_workers", r.skipped_workers},
            {"generate_s", r.generate_s},
            {"rows", rows},
            {"agreement_ok", r.agreement_ok},
            {"tolerance_c", r.tolerance_c},
            {"note", r.note}};
}

json to_json(const fea::ErrorReport& e) {
    return {{"elements", e.elements},
            {"h", e.h},
            {"max_nodal_error", e.max_nodal_error},
            {"max_midpoint_error", e.max_midpoint_error},
            {"max_sampled_error", e.max_sampled_error},
            {"l2_error", e.l2_error},
            {"max_temperature", e.max_temperature}};
}

json to_json(const hw::DeviceInfo& d) {
    return {{"id", d.id},
            {"kind", hw::to_string(d.kind)},
            {"model", d.model},
            {"usb_vid_pid", d.usb_vid_pid},
            {"host_link", d.host_link},
            {"io_voltage", d.io_voltage},
            {"serial", d.serial},
            {"simulated", d.simulated}};
}

json to_json(const hw::Capabilities& c) {
    json items = json::array();
    for (const auto& i : c.items) items.push_back({{"name", i.name}, {"detail", i.detail}});
    return {{"items", items}, {"general_compute", c.general_compute}};
}

json to_json(const hw::Status& s) {
    return {{"ok", s.ok()}, {"code", hw::to_string(s.code)}, {"message", s.message}, {"retryable", s.retryable}};
}

json to_json(const hw::TelemetryMessage& m) {
    json fields = json::object();
    for (const auto& [k, v] : m.fields) {
        const auto u = m.units.find(k);
        fields[k] = {{"value", v}, {"unit", u == m.units.end() ? "" : u->second}};
    }
    char crc[16];
    std::snprintf(crc, sizeof crc, "0x%08X", m.crc32);
    const bool crc_ok = hw::crc32(m.canonical_payload()) == m.crc32;
    return {{"device_id", m.device_id}, {"sequence", m.sequence}, {"device_time_ms", m.device_time_ms},
            {"fields", fields},         {"crc32", crc},           {"crc_ok", crc_ok},
            {"simulated", m.simulated}};
}

json to_json(const hw::ConnectOutcome& o) {
    json attempts = json::array();
    for (const auto& a : o.attempts)
        attempts.push_back({{"attempt", a.attempt}, {"status", to_json(a.status)}, {"backoff_ms", a.backoff_ms}});
    return {{"status", to_json(o.final_status)}, {"attempts", attempts}, {"recovered", o.recovered}};
}

json to_json(const bp::Params& p) { return {{"w", p.w}, {"b", p.b}, {"v", p.v}, {"c", p.c}}; }
json to_json(const bp::Gradients& g) { return {{"w", g.dw}, {"b", g.db}, {"v", g.dv}, {"c", g.dc}}; }

fea::RodProblem rod_from_json(const json& j) {
    fea::RodProblem p;
    auto read = [&](const char* key, double& dst, const char* unit) {
        if (!j.contains(key)) return;
        const json& v = j.at(key);
        if (!v.is_number()) throw std::invalid_argument(std::string("'") + key + "' must be a number in " + unit + ".");
        dst = v.get<double>();
    };
    if (!j.is_object()) return p;
    read("length", p.length, "m");
    read("area", p.area, "m^2");
    read("conductivity", p.conductivity, "W/(m K)");
    read("heat_gen", p.heat_gen, "W/m^3");
    read("t_left", p.t_left, "deg C");
    read("t_right", p.t_right, "deg C");
    return p;
}

json to_json(const fea::RodProblem& p) {
    return {{"length", p.length},   {"area", p.area},     {"conductivity", p.conductivity},
            {"heat_gen", p.heat_gen}, {"t_left", p.t_left}, {"t_right", p.t_right}};
}

} // namespace wb::app
