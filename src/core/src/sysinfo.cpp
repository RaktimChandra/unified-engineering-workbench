#include "wb/sysinfo.hpp"

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <sys/utsname.h>
#  include <unistd.h>
#endif

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#  include <intrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#  include <cpuid.h>
#endif

#ifndef WB_BUILD_FLAGS
#  define WB_BUILD_FLAGS "unknown"
#endif
#ifndef WB_BUILD_TYPE
#  define WB_BUILD_TYPE "unknown"
#endif

namespace wb::sys {
namespace {

std::string cpu_brand() {
    unsigned regs[12] = {0};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int r[4];
    __cpuid(r, 0x80000000);
    if (static_cast<unsigned>(r[0]) < 0x80000004) return "unknown";
    for (int i = 0; i < 3; ++i) {
        __cpuid(r, 0x80000002 + i);
        std::memcpy(&regs[i * 4], r, sizeof r);
    }
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (__get_cpuid_max(0x80000000, nullptr) < 0x80000004) return "unknown";
    for (unsigned i = 0; i < 3; ++i) {
        __get_cpuid(0x80000002 + i, &a, &b, &c, &d);
        regs[i * 4 + 0] = a; regs[i * 4 + 1] = b; regs[i * 4 + 2] = c; regs[i * 4 + 3] = d;
    }
#else
    return "unknown (non-x86)";
#endif
    char s[49] = {0};
    std::memcpy(s, regs, 48);
    std::string out(s);
    // Vendors pad the 48-byte brand string with spaces on either side
    // (AMD pads at the end: "AMD Ryzen 7 6800H with Radeon Graphics         ").
    const auto first = out.find_first_not_of(' ');
    if (first == std::string::npos) return "unknown";
    const auto last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

std::string compiler_id() {
    std::ostringstream o;
#if defined(__clang__)
    o << "Clang " << __clang_major__ << '.' << __clang_minor__ << '.' << __clang_patchlevel__;
#elif defined(__GNUC__)
    o << "GCC " << __GNUC__ << '.' << __GNUC_MINOR__ << '.' << __GNUC_PATCHLEVEL__;
#  if defined(__MINGW64__)
    o << " (MinGW-w64)";
#  endif
#elif defined(_MSC_VER)
    o << "MSVC " << _MSC_FULL_VER;
#else
    o << "unknown compiler";
#endif
    return o.str();
}

} // namespace

HostInfo query() {
    HostInfo h;
    h.cpu_model = cpu_brand();
    h.logical_cpus = std::thread::hardware_concurrency();
    h.compiler = compiler_id();
    h.build_flags = WB_BUILD_FLAGS;
    h.build_type = WB_BUILD_TYPE;
#if defined(_WIN32)
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof ms;
    if (GlobalMemoryStatusEx(&ms)) h.ram_bytes = ms.ullTotalPhys;

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len > 0) {
        std::string buf(len, '\0');
        auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(&buf[0]);
        if (GetLogicalProcessorInformationEx(RelationProcessorCore, info, &len)) {
            for (DWORD off = 0; off < len;) {
                auto* rec = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(&buf[off]);
                if (rec->Relationship == RelationProcessorCore) ++h.physical_cores;
                off += rec->Size;
            }
        }
    }
    // RtlGetVersion reports the real OS version (GetVersionEx is manifest-capped).
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")))) {
            OSVERSIONINFOW v{};
            v.dwOSVersionInfoSize = sizeof v;
            if (fn(&v) == 0)
                h.os = "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) +
                       "." + std::to_string(v.dwBuildNumber);
        }
        // Wine exports this symbol; genuine Windows does not.
        if (GetProcAddress(ntdll, "wine_get_version") != nullptr) {
            h.under_wine = true;
            h.os += " (Wine compatibility layer)";
        }
    }
    if (h.os.empty()) h.os = "Windows";
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && page > 0) h.ram_bytes = static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page);
    std::ifstream cpu("/proc/cpuinfo");
    std::set<std::pair<int, int>> cores;
    std::string line;
    int phys = 0, core = -1;
    while (std::getline(cpu, line)) {
        if (line.rfind("physical id", 0) == 0) phys = std::stoi(line.substr(line.find(':') + 1));
        else if (line.rfind("core id", 0) == 0) core = std::stoi(line.substr(line.find(':') + 1));
        else if (line.empty() && core >= 0) { cores.insert({phys, core}); core = -1; }
    }
    if (core >= 0) cores.insert({phys, core});
    h.physical_cores = static_cast<unsigned>(cores.size());
    utsname u{};
    if (uname(&u) == 0) h.os = std::string(u.sysname) + " " + u.release;
#endif
    return h;
}

} // namespace wb::sys
