#include "jobs.hpp"

#include <exception>

namespace wb::app {

const char* to_string(JobState s) {
    switch (s) {
    case JobState::queued: return "queued";
    case JobState::running: return "running";
    case JobState::done: return "done";
    case JobState::failed: return "failed";
    case JobState::cancelled: return "cancelled";
    }
    return "?";
}

void Job::set_progress(double f, const std::string& stage) {
    std::lock_guard<std::mutex> lk(mu_);
    progress_ = f < 0 ? 0 : f > 1 ? 1 : f;
    stage_ = stage;
}

JobState Job::state() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_;
}

nlohmann::json Job::status_json() const {
    std::lock_guard<std::mutex> lk(mu_);
    using ms = std::chrono::milliseconds;
    const auto now = std::chrono::steady_clock::now();
    nlohmann::json j{{"id", id_},
                     {"kind", kind_},
                     {"state", to_string(state_)},
                     {"progress", progress_},
                     {"stage", stage_},
                     {"cancel_requested", cancel_.requested()}};
    if (state_ != JobState::queued) {
        const auto end = (state_ == JobState::running) ? now : finished_;
        j["elapsed_ms"] = std::chrono::duration_cast<ms>(end - started_).count();
    }
    if (state_ == JobState::failed) j["error"] = error_;
    if (state_ == JobState::done) {
        j["result"] = output_.result;
        j["has_blob"] = !output_.blob.empty();
        nlohmann::json names = nlohmann::json::array();
        for (const auto& [name, content] : output_.exports) { (void)content; names.push_back(name); }
        j["exports"] = names;
    }
    return j;
}

bool Job::output(JobOutput& out) const {
    std::lock_guard<std::mutex> lk(mu_);
    if (state_ != JobState::done) return false;
    out = output_;
    return true;
}

std::shared_ptr<const void> Job::retained() const {
    std::lock_guard<std::mutex> lk(mu_);
    return state_ == JobState::done ? output_.retained : nullptr;
}

JobManager::JobManager(std::size_t threads, std::size_t keep) : keep_(keep) {
    for (std::size_t i = 0; i < threads; ++i) threads_.emplace_back([this] { worker(); });
}

JobManager::~JobManager() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
        for (auto& [job, body] : queue_) job->cancel_token().request();
        for (auto& [id, job] : jobs_) job->cancel_token().request();
    }
    cv_.notify_all();
    for (auto& t : threads_) t.join();
}

std::shared_ptr<Job> JobManager::submit(const std::string& kind, JobBody body) {
    std::lock_guard<std::mutex> lk(mu_);
    auto job = std::make_shared<Job>("j" + std::to_string(next_id_++), kind);
    jobs_[job->id()] = job;
    order_.push_back(job->id());
    while (order_.size() > keep_) { // forget the oldest finished jobs
        auto it = jobs_.find(order_.front());
        if (it != jobs_.end()) {
            const JobState st = it->second->state();
            if (st == JobState::queued || st == JobState::running) break;
            jobs_.erase(it);
        }
        order_.pop_front();
    }
    queue_.emplace_back(job, std::move(body));
    cv_.notify_one();
    return job;
}

std::shared_ptr<Job> JobManager::get(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = jobs_.find(id);
    return it == jobs_.end() ? nullptr : it->second;
}

bool JobManager::cancel(const std::string& id) {
    auto job = get(id);
    if (!job) return false;
    job->cancel_token().request();
    return true;
}

void JobManager::wait_idle() {
    std::unique_lock<std::mutex> lk(mu_);
    idle_cv_.wait(lk, [&] { return queue_.empty() && running_ == 0; });
}

void JobManager::worker() {
    for (;;) {
        std::shared_ptr<Job> job;
        JobBody body;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return stop_ || !queue_.empty(); });
            if (stop_) return;
            job = queue_.front().first;
            body = std::move(queue_.front().second);
            queue_.pop_front();
            ++running_;
        }
        {
            std::lock_guard<std::mutex> lk(job->mu_);
            job->started_ = std::chrono::steady_clock::now();
            job->state_ = job->cancel_.requested() ? JobState::cancelled : JobState::running;
            job->stage_ = job->cancel_.requested() ? "cancelled before start" : "running";
        }
        if (job->state() == JobState::running) {
            JobOutput out;
            JobState final_state = JobState::done;
            nlohmann::json error;
            try {
                out = body(*job);
                if (job->cancel_.requested()) final_state = JobState::cancelled; // finished, but caller asked to drop it
            } catch (const Cancelled&) {
                final_state = JobState::cancelled;
            } catch (const JobFailure& f) {
                final_state = JobState::failed;
                error = f.error;
            } catch (const std::exception& e) {
                final_state = JobState::failed;
                error = {{"code", "internal"}, {"message", e.what()}, {"hint", "See logs/workbench.log."}};
            }
            std::lock_guard<std::mutex> lk(job->mu_);
            job->finished_ = std::chrono::steady_clock::now();
            job->state_ = final_state;
            if (final_state == JobState::done) {
                // Publish the complete output in one step under the lock;
                // pollers see either nothing or the whole result.
                job->output_ = std::move(out);
                job->progress_ = 1.0;
                job->stage_ = "done";
            } else if (final_state == JobState::cancelled) {
                job->stage_ = "cancelled; no partial result kept";
            } else {
                job->error_ = error;
                job->stage_ = "failed";
            }
        } else {
            std::lock_guard<std::mutex> lk(job->mu_);
            job->finished_ = std::chrono::steady_clock::now();
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            --running_;
        }
        idle_cv_.notify_all();
    }
}

} // namespace wb::app
