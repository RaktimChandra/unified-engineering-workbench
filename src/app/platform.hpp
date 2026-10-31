// OS integration for the launcher: executable location, browser launch,
// process start time and a writable log location.
#pragma once

#include <filesystem>
#include <string>

namespace wb::platform {

std::filesystem::path executable_dir();
/// Opens the URL in the user's default browser. Returns false if it could not.
bool open_browser(const std::string& url);
/// Milliseconds since the OS created this process (true launch-to-ready
/// includes loader time before main()). Returns -1 if unavailable.
double ms_since_process_start();
/// Directory for logs: <exe>/logs if writable, otherwise the user temp dir.
std::filesystem::path log_dir(const std::filesystem::path& exe_dir);
/// Shows a blocking error dialog on Windows (no console exists there); stderr elsewhere.
void fatal_message(const std::string& title, const std::string& text);

} // namespace wb::platform
