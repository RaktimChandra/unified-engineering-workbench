// Import of ANSYS-style nodal temperature CSV exports (Task B, part ii).
// Schema: node_id,x_m,y_m,z_m,temperature_C  (SI metres, degrees Celsius).
#pragma once

#include "wb/common.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace wb::nodal {

inline constexpr std::array<const char*, 5> kColumns = {"node_id", "x_m", "y_m", "z_m", "temperature_C"};
inline constexpr double kAbsoluteZeroC = -273.15;

struct Limits {
    std::size_t max_bytes = 256ull * 1024 * 1024;
    std::size_t max_nodes = 5'000'000;
};

struct NodalField {
    std::vector<std::int64_t> node_id;
    std::vector<double> x, y, z;   ///< metres
    std::vector<double> temperature_c;
    double t_min = 0, t_max = 0, t_mean = 0;
    std::array<double, 3> bmin{0, 0, 0}, bmax{0, 0, 0};
    std::size_t size() const noexcept { return node_id.size(); }
};

struct ParseResult {
    bool ok = false;
    NodalField field;
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;
};

/// Parses and validates the CSV. Columns are matched by header name, so a
/// reordered export is accepted; missing or duplicated required columns are
/// errors, unknown extra columns produce a warning and are ignored.
ParseResult parse(std::string_view text, const Limits& limits = {}, const ProgressFn& progress = {},
                  const CancelToken* cancel = nullptr);

} // namespace wb::nodal
