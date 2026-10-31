#include "wb/stl.hpp"

#include "wb/text_scan.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace wb::stl {
namespace {

using text::NumberStatus;

/// Splits a line into at most `kMax` whitespace-separated tokens.
struct Tokens {
    static constexpr std::size_t kMax = 6;
    std::string_view t[kMax];
    std::size_t n = 0;
    bool overflow = false;
};

Tokens split(std::string_view line) {
    Tokens out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && text::is_space(line[i])) ++i;
        if (i >= line.size()) break;
        std::size_t j = i;
        while (j < line.size() && !text::is_space(line[j])) ++j;
        if (out.n == Tokens::kMax) { out.overflow = true; break; }
        out.t[out.n++] = line.substr(i, j - i);
        i = j;
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char x = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
        const char y = static_cast<char>(std::tolower(static_cast<unsigned char>(b[i])));
        if (x != y) return false;
    }
    return true;
}

struct V3 { double x, y, z; };
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
double norm(V3 a) { return std::sqrt(dot(a, a)); }

/// Exact-coordinate key used to weld shared vertices for topology checks.
struct VKey {
    float x, y, z;
    bool operator==(const VKey& o) const noexcept { return x == o.x && y == o.y && z == o.z; }
};
struct VKeyHash {
    std::size_t operator()(const VKey& k) const noexcept {
        std::uint32_t a, b, c;
        std::memcpy(&a, &k.x, 4); std::memcpy(&b, &k.y, 4); std::memcpy(&c, &k.z, 4);
        std::uint64_t h = 1469598103934665603ull;
        for (std::uint32_t v : {a, b, c}) { h ^= v; h *= 1099511628211ull; }
        return static_cast<std::size_t>(h);
    }
};

void compute_stats(Mesh& mesh, std::vector<Diagnostic>& warnings) {
    MeshStats& s = mesh.stats;
    const std::size_t nt = mesh.positions.size() / 9;
    s.triangles = nt;
    mesh.normals.assign(nt * 3, 0.0f);

    std::unordered_map<VKey, std::uint32_t, VKeyHash> index;
    index.reserve(nt * 2);
    std::vector<std::uint32_t> tri_idx(nt * 3);

    Bounds b;
    b.min = {INFINITY, INFINITY, INFINITY};
    b.max = {-INFINITY, -INFINITY, -INFINITY};

    for (std::size_t t = 0; t < nt; ++t) {
        const float* p = &mesh.positions[t * 9];
        V3 v[3];
        for (int k = 0; k < 3; ++k) {
            v[k] = {p[3 * k], p[3 * k + 1], p[3 * k + 2]};
            for (int a = 0; a < 3; ++a) {
                b.min[a] = std::min(b.min[a], static_cast<double>(p[3 * k + a]));
                b.max[a] = std::max(b.max[a], static_cast<double>(p[3 * k + a]));
            }
            const VKey key{p[3 * k], p[3 * k + 1], p[3 * k + 2]};
            auto [it, inserted] = index.try_emplace(key, static_cast<std::uint32_t>(index.size()));
            tri_idx[t * 3 + k] = it->second;
        }
        const V3 n = cross(sub(v[1], v[0]), sub(v[2], v[0]));
        const double len = norm(n);
        s.surface_area += 0.5 * len;
        // Signed volume of the tetrahedron (origin, v0, v1, v2).
        s.volume += dot(v[0], cross(v[1], v[2])) / 6.0;
        if (len <= 0.0) {
            ++s.degenerate;
        } else {
            mesh.normals[t * 3 + 0] = static_cast<float>(n.x / len);
            mesh.normals[t * 3 + 1] = static_cast<float>(n.y / len);
            mesh.normals[t * 3 + 2] = static_cast<float>(n.z / len);
        }
    }
    s.unique_vertices = index.size();
    s.bounds = b;

    // Edge use counts: closed 2-manifold <=> every undirected edge used exactly twice.
    std::unordered_map<std::uint64_t, std::uint32_t> edges;
    edges.reserve(nt * 3);
    for (std::size_t t = 0; t < nt; ++t) {
        for (int e = 0; e < 3; ++e) {
            std::uint32_t a = tri_idx[t * 3 + e], c = tri_idx[t * 3 + (e + 1) % 3];
            if (a == c) continue; // collapsed edge of a degenerate triangle
            if (a > c) std::swap(a, c);
            ++edges[(static_cast<std::uint64_t>(a) << 32) | c];
        }
    }
    for (const auto& [key, count] : edges) {
        (void)key;
        if (count == 1) ++s.boundary_edges;
        else if (count > 2) ++s.non_manifold_edges;
    }
    s.closed = nt > 0 && s.boundary_edges == 0 && s.non_manifold_edges == 0;
    if (!s.closed) s.volume = 0.0;
    else if (s.volume < 0.0) {
        warnings.push_back({"stl.inverted", 0,
                            "Mesh is closed but its signed volume is negative.",
                            "Facet winding is inside-out; most viewers still display it correctly."});
        s.volume = -s.volume;
    }

    if (s.degenerate > 0)
        warnings.push_back({"stl.degenerate", 0,
                            std::to_string(s.degenerate) + " zero-area triangle(s) found.",
                            "They are kept for display but contribute nothing to area/volume."});
    if (s.normal_mismatch > 0)
        warnings.push_back({"stl.normal_mismatch", 0,
                            std::to_string(s.normal_mismatch) +
                                " facet normal(s) disagree with the vertex winding (dot < 0).",
                            "Normals are recomputed from winding for shading; check the exporter."});
    if (!s.closed && nt > 0)
        warnings.push_back({"stl.open", 0,
                            "Mesh is not closed (" + std::to_string(s.boundary_edges) + " boundary, " +
                                std::to_string(s.non_manifold_edges) + " non-manifold edges).",
                            "Display works; enclosed volume is not reported for open meshes."});
}

} // namespace

bool looks_binary(std::string_view bytes) {
    if (bytes.size() < 84) return false;
    std::uint32_t count = 0;
    std::memcpy(&count, bytes.data() + 80, 4); // little-endian on every supported target
    const bool size_matches = 84ull + 50ull * count == bytes.size();
    // Some binary exporters start the 80-byte header with "solid", so the size
    // check wins; for ASCII files a coincidental size match is vanishingly rare
    // and we additionally require that no "facet" keyword appears early on.
    if (size_matches) {
        const std::string_view head = bytes.substr(0, std::min<std::size_t>(bytes.size(), 512));
        return head.find("facet") == std::string_view::npos;
    }
    for (std::size_t i = 0; i < std::min<std::size_t>(bytes.size(), 256); ++i)
        if (bytes[i] == '\0') return true; // ASCII STL never contains NUL bytes
    return false;
}

ParseResult parse_ascii(std::string_view text_in, const Limits& limits, const ProgressFn& progress,
                        const CancelToken* cancel) {
    ParseResult r;
    auto fail = [&](std::string code, std::size_t line, std::string msg, std::string hint) {
        r.ok = false;
        r.errors.push_back({std::move(code), line, std::move(msg), std::move(hint)});
        r.mesh = Mesh{};
        return r;
    };

    if (text_in.size() > limits.max_bytes)
        return fail("stl.too_large", 0,
                    "File is " + std::to_string(text_in.size() / (1024 * 1024)) +
                        " MiB, above the tested limit of " + std::to_string(limits.max_bytes / (1024 * 1024)) +
                        " MiB.",
                    "Decimate the mesh in your CAD tool or export a coarser tessellation.");
    if (looks_binary(text_in))
        return fail("stl.binary", 0, "This is a binary STL file; only ASCII STL is supported.",
                    "Re-export as ASCII STL (most CAD tools offer 'STL (ASCII)' in the export options).");

    const std::string_view text = text::strip_bom(text_in);
    if (text::trim(text).empty())
        return fail("stl.empty", 0, "The file is empty (0 bytes or whitespace only).",
                    "Choose a non-empty ASCII STL file; an export may have been interrupted.");

    enum class State { start, in_solid, in_facet, in_loop, after_loop, done };
    State st = State::start;
    text::LineReader reader(text);
    std::string_view line;
    std::size_t facet_line = 0;
    int vertices_in_loop = 0;
    double facet_normal[3] = {0, 0, 0};
    float tri[9];

    auto expect_numbers = [&](const Tokens& tk, std::size_t first, double* out,
                              std::size_t ln, const char* what) -> bool {
        for (std::size_t i = 0; i < 3; ++i) {
            const std::size_t idx = first + i;
            if (idx >= tk.n) {
                fail("stl.missing_value", ln, std::string(what) + " needs 3 numbers, found " +
                                                  std::to_string(tk.n > first ? tk.n - first : 0) + ".",
                     "Each '" + std::string(what) + "' line must have exactly three coordinates.");
                return false;
            }
            const auto status = text::parse_double(tk.t[idx], out[i]);
            if (status == NumberStatus::non_finite) {
                fail("stl.non_finite", ln, "Non-finite value '" + std::string(tk.t[idx]) + "' in " + what + ".",
                     "NaN/Inf coordinates usually come from a failed export; repair or re-export the mesh.");
                return false;
            }
            if (status != NumberStatus::ok) {
                fail("stl.bad_number", ln, "'" + std::string(tk.t[idx]) + "' is not a number (in " + what + ").",
                     "Coordinates must use '.' as decimal separator, e.g. 1.25e-3.");
                return false;
            }
        }
        if (tk.n > first + 3 || tk.overflow) {
            fail("stl.extra_tokens", ln, std::string("Unexpected extra values after ") + what + ".",
                 "Each line must contain exactly three numbers.");
            return false;
        }
        return true;
    };

    std::size_t checked = 0;
    while (reader.next(line)) {
        const std::size_t ln = reader.line_number();
        if ((++checked & 0xFFFF) == 0) {
            throw_if_cancelled(cancel);
            if (progress) progress(static_cast<double>(reader.bytes_consumed()) / text.size(), "parsing STL");
        }
        const Tokens tk = split(line);
        if (tk.n == 0) continue;
        const std::string_view kw = tk.t[0];

        switch (st) {
        case State::start:
            if (!iequals(kw, "solid"))
                return fail("stl.no_solid", ln, "Expected 'solid' at the start of the file, found '" +
                                                    std::string(kw) + "'.",
                            "This does not look like an ASCII STL file.");
            r.mesh.solid_name = tk.n > 1 ? std::string(text::trim(line.substr(line.find(tk.t[1])))) : "";
            st = State::in_solid;
            break;
        case State::in_solid:
            if (iequals(kw, "endsolid")) { st = State::done; break; }
            if (!iequals(kw, "facet") || tk.n < 2 || !iequals(tk.t[1], "normal"))
                return fail("stl.unexpected", ln, "Expected 'facet normal' or 'endsolid', found '" +
                                                      std::string(text::trim(line)) + "'.",
                            "The file may be truncated or hand-edited.");
            if (!expect_numbers(tk, 2, facet_normal, ln, "facet normal")) return r;
            facet_line = ln;
            st = State::in_facet;
            break;
        case State::in_facet:
            if (!(iequals(kw, "outer") && tk.n == 2 && iequals(tk.t[1], "loop")))
                return fail("stl.unexpected", ln, "Expected 'outer loop' after the facet on line " +
                                                      std::to_string(facet_line) + ".",
                            "Every facet must contain one 'outer loop' block.");
            vertices_in_loop = 0;
            st = State::in_loop;
            break;
        case State::in_loop:
            if (iequals(kw, "vertex")) {
                if (vertices_in_loop == 3)
                    return fail("stl.too_many_vertices", ln,
                                "Facet starting on line " + std::to_string(facet_line) +
                                    " has more than 3 vertices.",
                                "STL facets must be triangles; triangulate polygons before export.");
                double v[3];
                if (!expect_numbers(tk, 1, v, ln, "vertex")) return r;
                for (int a = 0; a < 3; ++a) tri[vertices_in_loop * 3 + a] = static_cast<float>(v[a]);
                ++vertices_in_loop;
            } else if (iequals(kw, "endloop")) {
                if (vertices_in_loop != 3)
                    return fail("stl.too_few_vertices", ln,
                                "Facet starting on line " + std::to_string(facet_line) + " has only " +
                                    std::to_string(vertices_in_loop) + " vertex/vertices.",
                                "STL facets need exactly 3 vertices.");
                st = State::after_loop;
            } else {
                return fail("stl.unexpected", ln, "Expected 'vertex' or 'endloop', found '" +
                                                      std::string(kw) + "'.",
                            "The facet block is malformed.");
            }
            break;
        case State::after_loop:
            if (!iequals(kw, "endfacet"))
                return fail("stl.unexpected", ln, "Expected 'endfacet', found '" + std::string(kw) + "'.",
                            "Close each facet with 'endfacet'.");
            if (r.mesh.positions.size() / 9 >= limits.max_triangles)
                return fail("stl.too_many_triangles", ln,
                            "More than " + std::to_string(limits.max_triangles) + " triangles.",
                            "Above the tested limit; decimate the mesh.");
            r.mesh.positions.insert(r.mesh.positions.end(), tri, tri + 9);
            {
                // Compare stored normal with winding to catch flipped exporters.
                const V3 a{tri[0], tri[1], tri[2]}, b{tri[3], tri[4], tri[5]}, c{tri[6], tri[7], tri[8]};
                const V3 n = cross(sub(b, a), sub(c, a));
                const V3 stored{facet_normal[0], facet_normal[1], facet_normal[2]};
                if (norm(stored) == 0.0) ++r.mesh.stats.zero_normals;
                else if (dot(n, stored) < 0.0) ++r.mesh.stats.normal_mismatch;
            }
            st = State::in_solid;
            break;
        case State::done:
            // Multiple solids in one file are legal; accept a following 'solid'.
            if (iequals(kw, "solid")) { st = State::in_solid; break; }
            return fail("stl.trailing", ln, "Unexpected content after 'endsolid': '" + std::string(kw) + "'.",
                        "Remove trailing text after the final 'endsolid'.");
        }
    }

    if (st != State::done)
        return fail("stl.truncated", reader.line_number(),
                    "File ended before 'endsolid' (state: " +
                        std::string(st == State::in_solid ? "between facets" : "inside a facet") + ").",
                    "The file is truncated; re-export or re-download it.");
    if (r.mesh.positions.empty())
        return fail("stl.no_facets", 0, "The solid is syntactically valid but contains no facets.",
                    "Nothing to display; export the body again with geometry selected.");

    const std::size_t zero_normals = r.mesh.stats.zero_normals;
    const std::size_t mismatch = r.mesh.stats.normal_mismatch;
    compute_stats(r.mesh, r.warnings);
    r.mesh.stats.zero_normals = zero_normals;
    r.mesh.stats.normal_mismatch = mismatch;
    if (mismatch > 0 && std::none_of(r.warnings.begin(), r.warnings.end(),
                                     [](const Diagnostic& d) { return d.code == "stl.normal_mismatch"; }))
        r.warnings.push_back({"stl.normal_mismatch", 0,
                              std::to_string(mismatch) + " facet normal(s) disagree with the vertex winding.",
                              "Normals are recomputed from winding for shading."});
    if (progress) progress(1.0, "STL parsed");
    r.ok = true;
    return r;
}

} // namespace wb::stl
