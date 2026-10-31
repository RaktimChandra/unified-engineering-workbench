#include "wb/nodal_csv.hpp"

#include "wb/text_scan.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <string>
#include <unordered_map>

namespace wb::nodal {
namespace {

/// Splits on ',' and strips surrounding whitespace and one level of double quotes.
void split_fields(std::string_view line, std::vector<std::string_view>& out) {
    out.clear();
    std::size_t start = 0;
    for (;;) {
        const auto comma = line.find(',', start);
        std::string_view f = text::trim(line.substr(start, comma == std::string_view::npos ? line.size() - start
                                                                                           : comma - start));
        if (f.size() >= 2 && f.front() == '"' && f.back() == '"') f = text::trim(f.substr(1, f.size() - 2));
        out.push_back(f);
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
}

std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

} // namespace

ParseResult parse(std::string_view text_in, const Limits& limits, const ProgressFn& progress,
                  const CancelToken* cancel) {
    ParseResult r;
    auto fail = [&](std::string code, std::size_t line, std::string msg, std::string hint) {
        r.ok = false;
        r.field = NodalField{};
        r.errors.push_back({std::move(code), line, std::move(msg), std::move(hint)});
        return r;
    };

    if (text_in.size() > limits.max_bytes)
        return fail("csv.too_large", 0, "File exceeds the tested limit of " +
                                            std::to_string(limits.max_bytes / (1024 * 1024)) + " MiB.",
                    "Export a subset of nodes or a coarser mesh.");
    const std::string_view text = text::strip_bom(text_in);
    if (text::trim(text).empty())
        return fail("csv.empty", 0, "The file is empty (0 bytes or whitespace only).",
                    "Expected a header 'node_id,x_m,y_m,z_m,temperature_C' followed by one row per node.");

    text::LineReader reader(text);
    std::string_view line;
    std::vector<std::string_view> fields;

    // ---- header -----------------------------------------------------------
    std::size_t header_line = 0;
    while (reader.next(line)) {
        if (!text::trim(line).empty()) { header_line = reader.line_number(); break; }
    }
    if (line.find(';') != std::string_view::npos && line.find(',') == std::string_view::npos)
        return fail("csv.semicolon", header_line, "The file is semicolon-delimited.",
                    "Re-export with ',' as delimiter and '.' as decimal separator (regional Excel settings "
                    "often switch both).");
    split_fields(line, fields);
    std::array<int, 5> col{-1, -1, -1, -1, -1};
    std::vector<std::string> unknown;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const std::string name = lower(fields[i]);
        bool matched = false;
        for (std::size_t c = 0; c < kColumns.size(); ++c) {
            if (name == lower(kColumns[c])) {
                if (col[c] != -1)
                    return fail("csv.duplicate_column", header_line,
                                std::string("Column '") + kColumns[c] + "' appears twice in the header.",
                                "Remove the duplicate column.");
                col[c] = static_cast<int>(i);
                matched = true;
            }
        }
        if (!matched) unknown.emplace_back(fields[i]);
    }
    for (std::size_t c = 0; c < kColumns.size(); ++c) {
        if (col[c] == -1) {
            // A numeric first row means the header is missing altogether.
            double probe;
            const bool numeric = !fields.empty() && text::parse_double(fields[0], probe) == text::NumberStatus::ok;
            return fail(numeric ? "csv.no_header" : "csv.missing_column", header_line,
                        numeric ? std::string("The first row contains numbers; the header row is missing.")
                                : std::string("Required column '") + kColumns[c] + "' is missing from the header.",
                        "The header must name: node_id,x_m,y_m,z_m,temperature_C (SI metres, degrees Celsius).");
        }
    }
    if (!unknown.empty()) {
        std::string list;
        for (const auto& u : unknown) list += (list.empty() ? "" : ", ") + u;
        r.warnings.push_back({"csv.extra_columns", header_line, "Ignoring unrecognised column(s): " + list + ".",
                              "Only the five schema columns are used."});
    }
    const std::size_t ncols = fields.size();

    // ---- rows ---------------------------------------------------------------
    NodalField& f = r.field;
    f.node_id.reserve(text.size() / 48);
    std::unordered_map<std::int64_t, std::size_t> seen;
    seen.reserve(text.size() / 48);
    std::size_t blank_lines = 0;
    std::size_t n = 0;

    while (reader.next(line)) {
        const std::size_t ln = reader.line_number();
        if ((++n & 0x3FFF) == 0) {
            throw_if_cancelled(cancel);
            if (progress) progress(static_cast<double>(reader.bytes_consumed()) / text.size(), "parsing CSV");
        }
        if (text::trim(line).empty()) { ++blank_lines; continue; }
        split_fields(line, fields);
        if (fields.size() != ncols)
            return fail("csv.column_count", ln,
                        "Row has " + std::to_string(fields.size()) + " field(s), header has " +
                            std::to_string(ncols) + ".",
                        "Check for a missing value or an unquoted comma in this row.");

        // node_id: positive integer (ANSYS node numbers start at 1).
        const std::string_view idtok = fields[static_cast<std::size_t>(col[0])];
        std::int64_t id = 0;
        auto [p, ec] = std::from_chars(idtok.data(), idtok.data() + idtok.size(), id);
        if (ec != std::errc() || p != idtok.data() + idtok.size() || id <= 0)
            return fail("csv.bad_node_id", ln, "node_id '" + std::string(idtok) + "' is not a positive integer.",
                        "Node numbers must be integers >= 1.");
        if (auto [it, inserted] = seen.try_emplace(id, ln); !inserted)
            return fail("csv.duplicate_node", ln,
                        "node_id " + std::to_string(id) + " already appeared on line " +
                            std::to_string(it->second) + ".",
                        "Each node must appear once; the export may contain several load steps.");

        double v[4];
        for (int c = 1; c <= 4; ++c) {
            const std::string_view tok = fields[static_cast<std::size_t>(col[c])];
            const auto st = text::parse_double(tok, v[c - 1]);
            if (st == text::NumberStatus::non_finite)
                return fail("csv.non_finite", ln, std::string("Non-finite value '") + std::string(tok) + "' in column " +
                                                      kColumns[c] + ".",
                            "NaN/Inf usually marks an unsolved node; fix the solution or remove the row.");
            if (st != text::NumberStatus::ok)
                return fail("csv.bad_number", ln,
                            std::string("'") + std::string(tok) + "' in column " + kColumns[c] + " is not a number.",
                            tok.find(',') != std::string_view::npos || tok.find('.') == std::string_view::npos
                                ? "Use '.' as the decimal separator, e.g. 293.15."
                                : "Expected a plain decimal or scientific number.");
        }
        if (v[3] < kAbsoluteZeroC)
            return fail("csv.below_absolute_zero", ln,
                        "Temperature " + std::to_string(v[3]) + " C is below absolute zero (-273.15 C).",
                        "Check the unit: this column must be degrees Celsius, not Kelvin offsets or Fahrenheit.");
        if (f.size() >= limits.max_nodes)
            return fail("csv.too_many_nodes", ln, "More than " + std::to_string(limits.max_nodes) + " nodes.",
                        "Above the tested limit.");
        f.node_id.push_back(id);
        f.x.push_back(v[0]);
        f.y.push_back(v[1]);
        f.z.push_back(v[2]);
        f.temperature_c.push_back(v[3]);
    }

    if (f.size() == 0)
        return fail("csv.no_rows", header_line, "The header is valid but the file contains no data rows.",
                    "Export at least one node.");
    if (blank_lines > 0)
        r.warnings.push_back({"csv.blank_lines", 0, std::to_string(blank_lines) + " blank line(s) skipped.", ""});

    // ---- statistics ---------------------------------------------------------
    f.t_min = *std::min_element(f.temperature_c.begin(), f.temperature_c.end());
    f.t_max = *std::max_element(f.temperature_c.begin(), f.temperature_c.end());
    double sum = 0;
    for (double t : f.temperature_c) sum += t;
    f.t_mean = sum / static_cast<double>(f.size());
    const std::vector<double>* axes[3] = {&f.x, &f.y, &f.z};
    for (int a = 0; a < 3; ++a) {
        f.bmin[a] = *std::min_element(axes[a]->begin(), axes[a]->end());
        f.bmax[a] = *std::max_element(axes[a]->begin(), axes[a]->end());
    }
    const double extent = std::max({f.bmax[0] - f.bmin[0], f.bmax[1] - f.bmin[1], f.bmax[2] - f.bmin[2]});
    if (extent > 100.0)
        r.warnings.push_back({"csv.large_extent", 0,
                              "Model extent is " + std::to_string(extent) + " m.",
                              "Coordinates may be in millimetres; the schema requires metres (x_m, y_m, z_m)."});
    if (progress) progress(1.0, "CSV parsed");
    r.ok = true;
    return r;
}

} // namespace wb::nodal
