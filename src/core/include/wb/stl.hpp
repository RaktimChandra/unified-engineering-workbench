// ASCII STL import and validation (Task B, part i).
#pragma once

#include "wb/common.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wb::stl {

struct Limits {
    std::size_t max_bytes = 256ull * 1024 * 1024; ///< tested up to this size, see docs/file_limits.md
    std::size_t max_triangles = 5'000'000;
};

struct Bounds {
    std::array<double, 3> min{0, 0, 0};
    std::array<double, 3> max{0, 0, 0};
};

/// Topology / quality diagnostics computed after a successful parse.
struct MeshStats {
    std::size_t triangles = 0;
    std::size_t unique_vertices = 0;   ///< after exact-coordinate welding
    std::size_t degenerate = 0;        ///< zero-area triangles (kept, reported)
    std::size_t normal_mismatch = 0;   ///< stored normal disagrees with vertex winding
    std::size_t zero_normals = 0;      ///< "facet normal 0 0 0" (legal, common in exporters)
    std::size_t boundary_edges = 0;    ///< edges used by exactly one triangle
    std::size_t non_manifold_edges = 0;///< edges used by more than two triangles
    bool closed = false;               ///< every edge shared by exactly two triangles
    double surface_area = 0.0;         ///< in file units squared
    double volume = 0.0;               ///< signed divergence-theorem volume, meaningful only if closed
    Bounds bounds;
};

struct Mesh {
    std::string solid_name;
    std::vector<float> positions; ///< 9 floats per triangle (v0,v1,v2), file units
    std::vector<float> normals;   ///< 3 floats per triangle, recomputed from winding
    MeshStats stats;
};

struct ParseResult {
    bool ok = false;
    Mesh mesh;
    std::vector<Diagnostic> errors;   ///< non-empty iff !ok (first fatal error first)
    std::vector<Diagnostic> warnings; ///< non-fatal quality findings
};

/// True if the buffer looks like a binary STL (80-byte header + count + 50 B/facet).
bool looks_binary(std::string_view bytes);

/// Parse an ASCII STL document held in memory. Never throws on bad input;
/// throws wb::Cancelled only if `cancel` fires.
ParseResult parse_ascii(std::string_view text, const Limits& limits = {},
                        const ProgressFn& progress = {}, const CancelToken* cancel = nullptr);

} // namespace wb::stl
