#include "platform.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <shellapi.h>
#else
#  include <cstdlib>
#  include <fstream>
#  include <sstream>
#  include <unistd.h>
#endif

namespace wb::platform {
namespace fs = std::filesystem;

fs::path executable_dir() {
#if defined(_WIN32)
    std::wstring buf(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, &buf[0], static_cast<DWORD>(buf.size()));
    buf.resize(n);
    return fs::path(buf).parent_path();
#else
    std::error_code ec;
    const fs::path p = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : p.parent_path();
#endif
}

bool open_browser(const std::string& url) {
#if defined(_WIN32)
    const std::wstring w(url.begin(), url.end()); // URL is plain ASCII (http://127.0.0.1:port/)
    const auto r = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return r > 32;
#else
    const std::string cmd = "xdg-open '" + url + "' >/dev/null 2>&1 &";
    return std::system(cmd.c_str()) == 0;
#endif
}

double ms_since_process_start() {
#if defined(_WIN32)
    FILETIME created, exited, kernel, user, now;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return -1;
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER a, b;
    a.LowPart = created.dwLowDateTime; a.HighPart = created.dwHighDateTime;
    b.LowPart = now.dwLowDateTime; b.HighPart = now.dwHighDateTime;
    return static_cast<double>(b.QuadPart - a.QuadPart) / 1e4; // 100 ns ticks -> ms
#else
    // /proc/self/stat field 22 = start time in clock ticks since boot.
    std::ifstream st("/proc/self/stat");
    std::string s((std::istreambuf_iterator<char>(st)), std::istreambuf_iterator<char>());
    const auto rp = s.rfind(')');
    if (rp == std::string::npos) return -1;
    std::istringstream rest(s.substr(rp + 2));
    std::string tok;
    unsigned long long start_ticks = 0;
    for (int field = 3; rest >> tok; ++field)
        if (field == 22) { start_ticks = std::stoull(tok); break; }
    std::ifstream up("/proc/uptime");
    double uptime = 0;
    up >> uptime;
    const double hz = static_cast<double>(sysconf(_SC_CLK_TCK));
    return (uptime - static_cast<double>(start_ticks) / hz) * 1000.0;
#endif
}

fs::path log_dir(const fs::path& exe_dir) {
    std::error_code ec;
    const fs::path preferred = exe_dir / "logs";
    fs::create_directories(preferred, ec);
    if (!ec) {
        std::ofstream probe(preferred / ".write_test");
        if (probe) {
            probe.close();
            fs::remove(preferred / ".write_test", ec);
            return preferred;
        }
    }
    const fs::path fallback = fs::temp_directory_path(ec) / "UnifiedEngineeringWorkbench";
    fs::create_directories(fallback, ec);
    return fallback;
}

void fatal_message(const std::string& title, const std::string& text) {
#if defined(_WIN32)
    MessageBoxA(nullptr, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
#else
    std::fprintf(stderr, "%s: %s\n", title.c_str(), text.c_str());
#endif
}

} // namespace wb::platform
