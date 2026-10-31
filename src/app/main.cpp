// Launch.exe entry point: locate bundled assets, start the loopback HTTP
// server, open the browser, and exit cleanly when the UI goes away.
//
//   Launch.exe [--port N] [--no-browser] [--web-root DIR] [--data-dir DIR]
//
// Double-clicking needs no arguments; the flags exist for development,
// automated screenshots and the integration tests.

#include "api.hpp"
#include "platform.hpp"

#include <httplib/httplib.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

/// First existing directory among candidates (packaged layout, then source tree).
fs::path first_existing(std::initializer_list<fs::path> candidates) {
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::is_directory(c, ec)) return fs::weakly_canonical(c, ec);
    }
    return {};
}

} // namespace

int main(int argc, char** argv) {
    const auto t_main = std::chrono::steady_clock::now();
    const double ms_before_main = wb::platform::ms_since_process_start();
    int port = 0;
    bool open = true;
    fs::path web_root, data_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--no-browser") open = false;
        else if (a == "--web-root" && i + 1 < argc) web_root = argv[++i];
        else if (a == "--data-dir" && i + 1 < argc) data_dir = argv[++i];
    }

    const fs::path exe = wb::platform::executable_dir();
    if (web_root.empty()) web_root = first_existing({exe / "app" / "web", exe / ".." / "src" / "frontend",
                                                     exe / ".." / ".." / "src" / "frontend"});
    if (data_dir.empty()) data_dir = first_existing({exe / "data", exe / ".." / "data", exe / ".." / ".." / "data"});
    if (web_root.empty() || !fs::exists(web_root / "index.html")) {
        wb::platform::fatal_message("Unified Engineering Workbench",
                                    "The user interface files were not found next to Launch.exe (expected app\\web\\index.html).\n"
                                    "Extract the complete ZIP archive, keeping its folder structure, and try again.");
        return 2;
    }

    const fs::path logs = wb::platform::log_dir(exe);
    std::ofstream log_file(logs / "workbench.log", std::ios::app);
    std::mutex log_mu;

    wb::app::ApiConfig cfg;
    cfg.web_root = web_root;
    cfg.data_dir = data_dir;
    wb::app::Api api(cfg);
    api.log().set_sink([&](const std::string& line) {
        std::lock_guard<std::mutex> lk(log_mu);
        if (log_file) log_file << line << std::endl;
#if !defined(_WIN32)
        std::cerr << line << '\n';
#endif
    });
    api.log().add("INFO", "Unified Engineering Workbench " + cfg.version + " starting");
    api.log().add("INFO", "web root: " + web_root.filename().string() + ", data: " +
                              (data_dir.empty() ? std::string("(none)") : data_dir.filename().string()) +
                              ", logs: " + logs.string());

    httplib::Server server;
    api.register_routes(server);

    // Loopback only: nothing is reachable from the network, and binding to
    // 127.0.0.1 does not trigger the Windows Defender Firewall prompt.
    const char* host = "127.0.0.1";
    int bound = -1;
    if (port > 0) bound = server.bind_to_port(host, port) ? port : -1;
    else {
        for (int candidate : {47615, 47616, 47617})
            if (server.bind_to_port(host, candidate)) { bound = candidate; break; }
        if (bound < 0) bound = server.bind_to_any_port(host);
    }
    if (bound <= 0) {
        wb::platform::fatal_message("Unified Engineering Workbench", "Could not open a local port on 127.0.0.1.");
        return 3;
    }

    const double ms_main_to_ready =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t_main).count();
    api.config().launch_to_ready_ms = ms_before_main >= 0 ? ms_before_main + ms_main_to_ready : ms_main_to_ready;
    const std::string url = "http://127.0.0.1:" + std::to_string(bound) + "/";
    api.log().add("INFO", "listening on " + url + " (process start -> HTTP ready: " +
                              std::to_string(static_cast<int>(api.config().launch_to_ready_ms)) + " ms)");
    std::cout << "READY " << url << std::endl; // consumed by automation; invisible in the GUI build

    std::thread http([&] { server.listen_after_bind(); });
    if (open && !wb::platform::open_browser(url))
        api.log().add("WARN", "could not open a browser automatically; open " + url + " manually");

    // Lifetime: stop when the UI asks, when the tab has been closed (beacon +
    // no heartbeat for 15 s, which tolerates reloads), or when no heartbeat has
    // arrived for 10 minutes (browsers throttle timers in background tabs, so
    // the long bound avoids killing a minimised but open tab).
    for (;;) {
        std::this_thread::sleep_for(250ms);
        if (api.shutdown_requested()) break;
        if (!api.heartbeat_seen()) continue;
        const double idle = api.seconds_since_heartbeat();
        if (api.page_closed() && idle > 15.0) { api.log().add("INFO", "browser tab closed; exiting"); break; }
        if (idle > 600.0) { api.log().add("INFO", "no heartbeat for 10 minutes; exiting"); break; }
    }
    server.stop();
    http.join();
    api.log().add("INFO", "stopped cleanly");
    return 0;
}
