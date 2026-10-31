// Shared vocabulary types for the workbench core: diagnostics, progress and
// cooperative cancellation. Nothing in wb_core depends on HTTP, JSON or UI code.
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace wb {

/// One actionable problem found while validating user input.
/// `line` is 1-based; 0 means "whole file / not line specific".
struct Diagnostic {
    std::string code;     ///< stable machine-readable id, e.g. "stl.non_finite"
    std::size_t line = 0; ///< 1-based source line, 0 if not applicable
    std::string message;  ///< what is wrong
    std::string hint;     ///< what the user can do about it
};

/// Cooperative cancellation flag shared between a job and its requester.
class CancelToken {
public:
    void request() noexcept { flag_.store(true, std::memory_order_relaxed); }
    bool requested() const noexcept { return flag_.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> flag_{false};
};

/// Progress callback: fraction in [0,1] plus a short human-readable stage.
using ProgressFn = std::function<void(double fraction, const std::string& stage)>;

/// Thrown by long-running core routines when a CancelToken fires. Callers
/// discard partial output, so a cancelled job never publishes a half result.
struct Cancelled {};

inline void throw_if_cancelled(const CancelToken* token) {
    if (token != nullptr && token->requested()) throw Cancelled{};
}

} // namespace wb
