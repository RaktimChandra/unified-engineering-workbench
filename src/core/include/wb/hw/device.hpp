// Hardware abstraction for the embedded integration bench (Task E).
//
// Every adapter in this build is a SIMULATION. Nothing here opens a USB
// device; the mock bus reports fixed descriptors that mirror what the real
// boards enumerate as, so the UI and state machine can be exercised offline.
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wb::hw {

enum class DeviceKind { microcontroller, fpga, jtag_probe };
const char* to_string(DeviceKind k);

enum class LinkState { disconnected, connecting, connected, fault };
const char* to_string(LinkState s);

struct DeviceInfo {
    std::string id;           ///< stable id used by the UI, e.g. "mcu0"
    DeviceKind kind{};
    std::string model;        ///< board / probe name
    std::string usb_vid_pid;  ///< what the real hardware enumerates as
    std::string host_link;    ///< e.g. "USB CDC-ACM virtual COM @115200 8N1"
    std::string io_voltage;   ///< signalling level assumed on the target side
    std::string serial;
    bool simulated = true;
};

struct Capability {
    std::string name;   ///< e.g. "telemetry.stream"
    std::string detail; ///< human-readable explanation
};

struct Capabilities {
    std::vector<Capability> items;
    bool general_compute = false; ///< can it run user workloads? (never true for a JTAG probe)
};

enum class ErrorCode { none, timeout, not_found, not_connected, link_lost, busy };
const char* to_string(ErrorCode e);

struct Status {
    ErrorCode code = ErrorCode::none;
    std::string message;
    bool retryable = false;
    bool ok() const noexcept { return code == ErrorCode::none; }
};

struct TelemetryMessage {
    std::string device_id;
    std::uint32_t sequence = 0;
    std::uint64_t device_time_ms = 0;            ///< simulated device uptime
    std::map<std::string, double> fields;        ///< name -> value
    std::map<std::string, std::string> units;    ///< name -> unit
    std::uint32_t crc32 = 0;                     ///< over the canonical payload
    bool simulated = true;
    std::string canonical_payload() const;       ///< deterministic text the CRC covers
};

std::uint32_t crc32(const std::string& bytes);

/// Fault injection used by tests and by the UI's "inject fault" control.
struct FaultPlan {
    int connect_timeouts = 0;      ///< next N connect attempts time out
    bool drop_link_on_next_poll = false;
};

class IDeviceAdapter {
public:
    virtual ~IDeviceAdapter() = default;
    virtual const DeviceInfo& info() const = 0;
    virtual Capabilities capabilities() const = 0;
    virtual Status connect(std::chrono::milliseconds timeout) = 0;
    virtual Status disconnect() = 0;
    virtual Status poll_telemetry(TelemetryMessage& out) = 0;
    virtual LinkState state() const = 0;
    virtual void set_fault_plan(const FaultPlan& plan) = 0;
};

/// Creates the three simulated adapters. `simulated_latency` is how long a
/// healthy connect takes; tests pass 0 to stay fast and deterministic.
std::unique_ptr<IDeviceAdapter> make_mock_mcu(std::chrono::milliseconds simulated_latency);
std::unique_ptr<IDeviceAdapter> make_mock_fpga(std::chrono::milliseconds simulated_latency);
std::unique_ptr<IDeviceAdapter> make_mock_jtag(std::chrono::milliseconds simulated_latency);

struct RetryPolicy {
    int max_attempts = 3;
    std::chrono::milliseconds connect_timeout{250};
    std::chrono::milliseconds initial_backoff{100}; ///< doubled after each failure
    bool sleep = true;                               ///< false in unit tests
};

struct ConnectAttempt {
    int attempt = 0;
    Status status;
    long long backoff_ms = 0;
};

struct ConnectOutcome {
    Status final_status;
    std::vector<ConnectAttempt> attempts;
    bool recovered = false; ///< succeeded after at least one failure
};

/// Owns adapters, serialises access, and implements retry/backoff recovery.
class DeviceManager {
public:
    explicit DeviceManager(std::chrono::milliseconds simulated_latency = std::chrono::milliseconds(40));

    std::vector<DeviceInfo> discover(); ///< enumerate the (mock) bus
    Capabilities capabilities(const std::string& id);
    ConnectOutcome connect(const std::string& id, const RetryPolicy& policy = {});
    Status disconnect(const std::string& id);
    Status poll(const std::string& id, TelemetryMessage& out);
    LinkState state(const std::string& id);
    void inject(const std::string& id, const FaultPlan& plan);

private:
    struct Slot {
        std::unique_ptr<IDeviceAdapter> adapter;
        std::unique_ptr<std::mutex> mu; ///< per-device lock: a slow connect never blocks other devices
    };
    Slot* find(const std::string& id);
    std::vector<Slot> devices_; ///< fixed after construction, so lookups need no lock
};

} // namespace wb::hw
