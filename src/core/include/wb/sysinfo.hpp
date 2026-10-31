// Host/build description for reproducible performance reports.
#pragma once

#include <cstdint>
#include <string>

namespace wb::sys {

struct HostInfo {
    std::string cpu_model;          ///< CPUID brand string, or "unknown"
    unsigned logical_cpus = 0;      ///< std::thread::hardware_concurrency()
    unsigned physical_cores = 0;    ///< 0 if the OS does not expose it
    std::uint64_t ram_bytes = 0;
    std::string os;                 ///< e.g. "Windows 10.0.26100" or "Linux 6.8"
    bool under_wine = false;        ///< Windows binary running on Wine (timings not representative)
    std::string compiler;           ///< compiler id + version
    std::string build_flags;        ///< optimisation flags baked in by CMake
    std::string build_type;
};

HostInfo query();

} // namespace wb::sys
