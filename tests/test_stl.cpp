#include <doctest/doctest.h>

#include "test_support.hpp"

#include "wb/stl.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

using namespace wb::stl;

namespace {
using wb::test::slurp;
const char* kTetra =
    "solid tetra\n"
    " facet normal 0 0 -1\n  outer loop\n   vertex 0 0 0\n   vertex 0 1 0\n   vertex 1 0 0\n  endloop\n endfacet\n"
    " facet normal 0 -1 0\n  outer loop\n   vertex 0 0 0\n   vertex 1 0 0\n   vertex 0 0 1\n  endloop\n endfacet\n"
    " facet normal -1 0 0\n  outer loop\n   vertex 0 0 0\n   vertex 0 0 1\n   vertex 0 1 0\n  endloop\n endfacet\n"
    " facet normal 0.577 0.577 0.577\n  outer loop\n   vertex 1 0 0\n   vertex 0 1 0\n   vertex 0 0 1\n  endloop\n endfacet\n"
    "endsolid tetra\n";
} // namespace

TEST_SUITE("stl") {

TEST_CASE("closed tetrahedron: topology, area and volume are exact") {
    const auto r = parse_ascii(kTetra);
    REQUIRE(r.ok);
    CHECK(r.mesh.solid_name == "tetra");
    CHECK(r.mesh.stats.triangles == 4);
    CHECK(r.mesh.stats.unique_vertices == 4);
    CHECK(r.mesh.stats.closed);
    CHECK(r.mesh.stats.boundary_edges == 0);
    CHECK(r.mesh.stats.volume == doctest::Approx(1.0 / 6.0).epsilon(1e-12));
    CHECK(r.mesh.stats.surface_area == doctest::Approx(1.5 + std::sqrt(3.0) / 2.0).epsilon(1e-6));
    CHECK(r.mesh.stats.normal_mismatch == 0);
    CHECK(r.warnings.empty());
}

TEST_CASE("bundled heat-sink example is closed with volume 720 mm^2 x 40 mm") {
    const auto r = parse_ascii(slurp("heatsink_fins.stl"));
    REQUIRE(r.ok);
    CHECK(r.mesh.stats.closed);
    CHECK(r.mesh.stats.non_manifold_edges == 0);
    CHECK(r.mesh.stats.volume == doctest::Approx(28800.0).epsilon(1e-9));
    CHECK(r.mesh.stats.bounds.max[0] == doctest::Approx(60.0));
    CHECK(r.mesh.stats.bounds.max[1] == doctest::Approx(36.0));
    CHECK(r.mesh.stats.degenerate == 0);
}

TEST_CASE("CRLF line endings, BOM, mixed case keywords and '+' signs are accepted") {
    std::string s = "\xEF\xBB\xBFSOLID x\r\nFacet Normal 0 0 +1\r\nOuter Loop\r\nVertex 0 0 0\r\nvertex 1e0 0 0\r\n"
                    "vertex 0 1.0E+00 0\r\nendloop\r\nendfacet\r\nendsolid x\r\n";
    const auto r = parse_ascii(s);
    REQUIRE(r.ok);
    CHECK(r.mesh.stats.triangles == 1);
    CHECK_FALSE(r.mesh.stats.closed); // single triangle -> open mesh warning
    CHECK(r.warnings.size() == 1);
}

TEST_CASE("flipped stored normal is reported, not fatal") {
    const auto r = parse_ascii("solid f\nfacet normal 0 0 -1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\n"
                               "endloop\nendfacet\nendsolid f\n");
    REQUIRE(r.ok);
    CHECK(r.mesh.stats.normal_mismatch == 1);
    CHECK(r.mesh.normals[2] == doctest::Approx(1.0f)); // recomputed from winding
}

TEST_CASE("malformed inputs are rejected with code and line") {
    struct Case { const char* text; const char* code; std::size_t line; };
    const Case cases[] = {
        {"", "stl.empty", 0},
        {"   \n\t\n", "stl.empty", 0},
        {"hello\n", "stl.no_solid", 1},
        {"solid a\nendsolid a\n", "stl.no_facets", 0},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 inf\n", "stl.non_finite", 4},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 nan\n", "stl.non_finite", 4},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 1e999\n", "stl.non_finite", 4},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0\n", "stl.missing_value", 4},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 1 2\n", "stl.extra_tokens", 4},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0,5 1\n", "stl.bad_number", 4},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nendloop\n", "stl.too_few_vertices", 6},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 1 1 0\nvertex 0 1 0\n",
         "stl.too_many_vertices", 7},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 1 1 0\nendloop\n", "stl.truncated", 7},
        {"solid a\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 1 1 0\nendloop\nendfacet\nendsolid\nxyz\n",
         "stl.trailing", 10},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        const auto r = parse_ascii(c.text);
        REQUIRE_FALSE(r.ok);
        CHECK(r.errors.front().code == c.code);
        CHECK(r.errors.front().line == c.line);
        CHECK_FALSE(r.errors.front().hint.empty());
        CHECK(r.mesh.positions.empty()); // no partial geometry leaks out
    }
}

TEST_CASE("binary STL is detected even when its header starts with 'solid'") {
    std::string b(84 + 50, '\0');
    b.replace(0, 5, "solid");
    const std::uint32_t one = 1;
    std::memcpy(&b[80], &one, 4);
    CHECK(looks_binary(b));
    const auto r = parse_ascii(b);
    REQUIRE_FALSE(r.ok);
    CHECK(r.errors[0].code == "stl.binary");
    CHECK(parse_ascii(slurp("malformed/stl_binary.stl")).errors[0].code == "stl.binary");
    CHECK_FALSE(looks_binary(kTetra));
}

TEST_CASE("size and triangle limits are enforced") {
    Limits lim;
    lim.max_bytes = 100;
    CHECK(parse_ascii(kTetra, lim).errors[0].code == "stl.too_large");
    Limits tri;
    tri.max_triangles = 3;
    CHECK(parse_ascii(kTetra, tri).errors[0].code == "stl.too_many_triangles");
}

TEST_CASE("cancellation throws and does not return partial output") {
    std::string big = "solid big\n";
    for (int i = 0; i < 20000; ++i)
        big += "facet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n";
    big += "endsolid big\n";
    wb::CancelToken t;
    t.request();
    CHECK_THROWS_AS(parse_ascii(big, {}, {}, &t), wb::Cancelled);
}

} // TEST_SUITE
