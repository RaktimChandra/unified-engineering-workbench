// Locates the bundled data/ folder without baking an absolute path into the
// test binary: $WB_DATA_DIR if set, else ./data, ../data, ... (up to 4 levels)
// relative to the working directory. Works from build/, from the source root
// and from app/bin/ inside the packaged ZIP.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace wb::test {

inline std::filesystem::path data_dir() {
    if (const char* env = std::getenv("WB_DATA_DIR")) return env;
    std::filesystem::path p = std::filesystem::current_path();
    for (int up = 0; up <= 4; ++up, p = p.parent_path())
        if (std::filesystem::exists(p / "data" / "manifest.json")) return p / "data";
    return "data";
}

inline std::string slurp(const std::string& rel) {
    std::ifstream f(data_dir() / rel, std::ios::binary);
    std::ostringstream o;
    o << f.rdbuf();
    return o.str();
}

} // namespace wb::test
