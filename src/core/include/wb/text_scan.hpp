// Small, locale-independent text helpers shared by the STL and CSV parsers.
#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <string_view>

namespace wb::text {

inline bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

inline std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && is_space(s.front())) s.remove_prefix(1);
    while (!s.empty() && is_space(s.back())) s.remove_suffix(1);
    return s;
}

/// Strips a UTF-8 byte-order mark if present (Excel writes one).
inline std::string_view strip_bom(std::string_view s) noexcept {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB && static_cast<unsigned char>(s[2]) == 0xBF)
        s.remove_prefix(3);
    return s;
}

enum class NumberStatus { ok, not_a_number, non_finite };

/// Parses the whole token as a double using std::from_chars, which, unlike
/// strtod/stod, ignores the process locale (a "de_DE" locale would otherwise
/// turn "1.5" into 1). Accepts an optional leading '+', which from_chars rejects.
inline NumberStatus parse_double(std::string_view token, double& out) noexcept {
    if (!token.empty() && token.front() == '+') token.remove_prefix(1);
    if (token.empty()) return NumberStatus::not_a_number;
    const char* first = token.data();
    const char* last = token.data() + token.size();
    auto [ptr, ec] = std::from_chars(first, last, out, std::chars_format::general);
    if (ec == std::errc::result_out_of_range) return NumberStatus::non_finite;
    if (ec != std::errc() || ptr != last) return NumberStatus::not_a_number;
    if (!std::isfinite(out)) return NumberStatus::non_finite; // "nan", "inf" parse successfully
    return NumberStatus::ok;
}

/// Iterates lines while counting them; handles LF and CRLF.
class LineReader {
public:
    explicit LineReader(std::string_view text) noexcept : rest_(text) {}

    bool next(std::string_view& line) noexcept {
        if (rest_.empty()) return false;
        const auto nl = rest_.find('\n');
        if (nl == std::string_view::npos) {
            line = rest_;
            rest_ = {};
        } else {
            line = rest_.substr(0, nl);
            rest_.remove_prefix(nl + 1);
        }
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        ++number_;
        consumed_ = total_ - rest_.size();
        return true;
    }
    std::size_t line_number() const noexcept { return number_; }
    std::size_t bytes_consumed() const noexcept { return consumed_; }

private:
    std::string_view rest_;
    std::size_t number_ = 0;
    std::size_t total_ = rest_.size();
    std::size_t consumed_ = 0;
};

} // namespace wb::text
