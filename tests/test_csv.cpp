#include <doctest/doctest.h>

#include "wb/nodal_csv.hpp"

#include <fstream>
#include <sstream>

using namespace wb::nodal;

namespace {
const std::string kHdr = "node_id,x_m,y_m,z_m,temperature_C\n";
std::string slurp(const std::string& rel) {
    std::ifstream f(std::string(WB_DATA_DIR) + "/" + rel, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}
} // namespace

TEST_SUITE("nodal_csv") {

TEST_CASE("valid file: values, statistics and bounds") {
    const auto r = parse(kHdr + "1,0,0,0,20\n2,0.5,0,0,40\n3,1,0.25,-0.1,30.5\n");
    REQUIRE(r.ok);
    CHECK(r.field.size() == 3);
    CHECK(r.field.node_id[2] == 3);
    CHECK(r.field.t_min == 20.0);
    CHECK(r.field.t_max == 40.0);
    CHECK(r.field.t_mean == doctest::Approx(30.1666666667));
    CHECK(r.field.bmin[2] == doctest::Approx(-0.1));
    CHECK(r.field.bmax[0] == doctest::Approx(1.0));
}

TEST_CASE("reordered columns, quotes, BOM, CRLF and blank lines are tolerated") {
    const auto r = parse("\xEF\xBB\xBF\"temperature_C\", node_id ,z_m,y_m,x_m\r\n\r\n25.5,7,3,2,1\r\n");
    REQUIRE(r.ok);
    CHECK(r.field.node_id[0] == 7);
    CHECK(r.field.x[0] == 1.0);
    CHECK(r.field.z[0] == 3.0);
    CHECK(r.field.temperature_c[0] == 25.5);
    CHECK(r.warnings.size() == 1); // blank line notice
}

TEST_CASE("extra columns are ignored with a warning") {
    const auto r = parse("node_id,x_m,y_m,z_m,temperature_C,heat_flux\n1,0,0,0,20,5\n");
    REQUIRE(r.ok);
    CHECK(r.warnings[0].code == "csv.extra_columns");
}

TEST_CASE("bundled synthetic example parses") {
    const auto r = parse(slurp("heatsink_nodal_temperature_SYNTHETIC.csv"));
    REQUIRE(r.ok);
    CHECK(r.field.size() > 1000);
    CHECK(r.field.t_max <= 85.0);
    CHECK(r.field.t_min >= 25.0);
}

TEST_CASE("malformed inputs are rejected with code and line") {
    struct Case { std::string text; const char* code; std::size_t line; };
    const Case cases[] = {
        {"", "csv.empty", 0},
        {kHdr, "csv.no_rows", 1},
        {"id,x,y,z,T\n1,0,0,0,20\n", "csv.missing_column", 1},
        {"1,0,0,0,20\n", "csv.no_header", 1},
        {"node_id,x_m,x_m,y_m,z_m,temperature_C\n", "csv.duplicate_column", 1},
        {kHdr + "1,0,0,0,nan\n", "csv.non_finite", 2},
        {kHdr + "1,0,0,0,20\n2,inf,0,0,20\n", "csv.non_finite", 3},
        {kHdr + "1,0,0,0,20\n1,1,0,0,20\n", "csv.duplicate_node", 3},
        {kHdr + "0,0,0,0,20\n", "csv.bad_node_id", 2},
        {kHdr + "1.5,0,0,0,20\n", "csv.bad_node_id", 2},
        {kHdr + "1,0,0,0\n", "csv.column_count", 2},
        {kHdr + "1,0,0,0,abc\n", "csv.bad_number", 2},
        {kHdr + "1,0,0,0,-274\n", "csv.below_absolute_zero", 2},
        {"node_id;x_m;y_m;z_m;temperature_C\n1;0;0;0;20,5\n", "csv.semicolon", 1},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        const auto r = parse(c.text);
        REQUIRE_FALSE(r.ok);
        CHECK(r.errors.front().code == c.code);
        CHECK(r.errors.front().line == c.line);
        CHECK(r.field.size() == 0);
    }
}

TEST_CASE("millimetre-looking coordinates trigger a unit warning") {
    const auto r = parse(kHdr + "1,0,0,0,20\n2,250,0,0,21\n");
    REQUIRE(r.ok);
    CHECK(r.warnings.back().code == "csv.large_extent");
}

TEST_CASE("node limit is enforced") {
    Limits lim;
    lim.max_nodes = 1;
    CHECK(parse(kHdr + "1,0,0,0,20\n2,0,0,0,20\n", lim).errors[0].code == "csv.too_many_nodes");
}

} // TEST_SUITE
