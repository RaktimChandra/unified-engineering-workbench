// HTTP/JSON transport for the workbench. Maps REST routes onto wb_core calls
// and background jobs; contains no numerical logic of its own.
#pragma once

#include "jobs.hpp"
#include "wb/hw/device.hpp"

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace httplib { class Server; }

namespace wb::app {

struct ApiConfig {
    std::filesystem::path web_root;   ///< static frontend files
    std::filesystem::path data_dir;   ///< bundled example data
    std::size_t max_upload_bytes = 256ull * 1024 * 1024;
    std::chrono::milliseconds device_latency{60};
    double launch_to_ready_ms = 0;    ///< filled in by main() once listening
    std::string version = "1.0.0";
};

/// Thread-safe in-memory log mirrored to the UI's status console.
class LogBuffer {
public:
    using Sink = std::function<void(const std::string&)>;
    void set_sink(Sink s);
    void add(const std::string& level, const std::string& msg);
    nlohmann::json since(std::size_t seq) const;

private:
    mutable std::mutex mu_;
    std::deque<nlohmann::json> lines_;
    std::size_t next_ = 1;
    Sink sink_;
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
};

class Api {
public:
    explicit Api(ApiConfig cfg);
    void register_routes(httplib::Server& server);
    ApiConfig& config() { return cfg_; }
    LogBuffer& log() { return log_; }
    JobManager& jobs() { return jobs_; }

    /// Seconds since the browser last checked in; used for auto-shutdown.
    double seconds_since_heartbeat() const;
    bool heartbeat_seen() const { return heartbeat_seen_.load(); }
    bool shutdown_requested() const { return shutdown_.load(); }
    bool page_closed() const { return page_closed_.load(); }

private:
    ApiConfig cfg_;
    LogBuffer log_;
    JobManager jobs_{2};
    hw::DeviceManager devices_;
    std::atomic<bool> shutdown_{false}, heartbeat_seen_{false}, page_closed_{false};
    std::atomic<long long> last_heartbeat_ms_{0};
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
};

} // namespace wb::app
