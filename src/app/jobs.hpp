// Background job execution for the HTTP layer. Long computations run on a
// small bounded executor so request threads (and therefore the UI) never block;
// results are published atomically only when a job completes successfully.
#pragma once

#include "wb/common.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace wb::app {

enum class JobState { queued, running, done, failed, cancelled };
const char* to_string(JobState s);

/// What a job body produces. `blob` carries large binary arrays (mesh
/// vertices, point clouds) so they are not inflated by JSON encoding.
struct JobOutput {
    nlohmann::json result;
    std::string blob;
    std::map<std::string, std::string> exports; ///< filename -> file content (CSV/SVG/TXT)
    std::shared_ptr<const void> retained;       ///< parsed model kept for follow-up queries (node inspection)
};

/// Thrown by a job body to report a user-facing failure with diagnostics.
struct JobFailure {
    nlohmann::json error; ///< {code, message, hint, line?, diagnostics?}
};

class Job {
public:
    Job(std::string id, std::string kind) : id_(std::move(id)), kind_(std::move(kind)) {}
    const std::string& id() const { return id_; }
    const std::string& kind() const { return kind_; }
    CancelToken& cancel_token() { return cancel_; }

    void set_progress(double f, const std::string& stage);
    nlohmann::json status_json() const;            ///< safe to call from any thread
    bool output(JobOutput& out) const;             ///< copies output if state == done
    std::shared_ptr<const void> retained() const;  ///< cheap access without copying blobs
    JobState state() const;

private:
    friend class JobManager;
    std::string id_, kind_;
    mutable std::mutex mu_;
    JobState state_ = JobState::queued;
    double progress_ = 0;
    std::string stage_ = "queued";
    nlohmann::json error_;
    JobOutput output_;
    std::chrono::steady_clock::time_point created_ = std::chrono::steady_clock::now(), started_, finished_;
    CancelToken cancel_;
};

using JobBody = std::function<JobOutput(Job& job)>;

class JobManager {
public:
    explicit JobManager(std::size_t threads = 2, std::size_t keep = 64);
    ~JobManager();
    std::shared_ptr<Job> submit(const std::string& kind, JobBody body);
    std::shared_ptr<Job> get(const std::string& id) const;
    bool cancel(const std::string& id);
    void wait_idle(); ///< used by tests

private:
    void worker();
    mutable std::mutex mu_;
    std::condition_variable cv_, idle_cv_;
    std::deque<std::pair<std::shared_ptr<Job>, JobBody>> queue_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    std::deque<std::string> order_;
    std::vector<std::thread> threads_;
    std::size_t keep_, running_ = 0, next_id_ = 1;
    bool stop_ = false;
};

} // namespace wb::app
