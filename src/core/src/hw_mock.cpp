#include "wb/hw/device.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <thread>

namespace wb::hw {

const char* to_string(DeviceKind k) {
    switch (k) {
    case DeviceKind::microcontroller: return "microcontroller";
    case DeviceKind::fpga: return "fpga";
    case DeviceKind::jtag_probe: return "jtag_probe";
    }
    return "?";
}

const char* to_string(LinkState s) {
    switch (s) {
    case LinkState::disconnected: return "disconnected";
    case LinkState::connecting: return "connecting";
    case LinkState::connected: return "connected";
    case LinkState::fault: return "fault";
    }
    return "?";
}

const char* to_string(ErrorCode e) {
    switch (e) {
    case ErrorCode::none: return "none";
    case ErrorCode::timeout: return "timeout";
    case ErrorCode::not_found: return "not_found";
    case ErrorCode::not_connected: return "not_connected";
    case ErrorCode::link_lost: return "link_lost";
    case ErrorCode::busy: return "busy";
    }
    return "?";
}

std::uint32_t crc32(const std::string& bytes) {
    // IEEE 802.3 polynomial, reflected; same CRC as zlib/Ethernet. Check value
    // for "123456789" is 0xCBF43926 (asserted in tests).
    std::uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : bytes) {
        c ^= b;
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

std::string TelemetryMessage::canonical_payload() const {
    std::ostringstream o;
    o << device_id << '|' << sequence << '|' << device_time_ms;
    char buf[64];
    for (const auto& [k, v] : fields) {
        std::snprintf(buf, sizeof buf, "%.6f", v);
        o << '|' << k << '=' << buf;
    }
    return o.str();
}

namespace {

/// Shared behaviour for all mock adapters: state machine, fault plan,
/// simulated latency and telemetry framing. Subclasses supply descriptors
/// and the physics of their fake sensor values.
class MockAdapterBase : public IDeviceAdapter {
public:
    MockAdapterBase(DeviceInfo info, std::chrono::milliseconds latency) : info_(std::move(info)), latency_(latency) {}

    const DeviceInfo& info() const override { return info_; }
    LinkState state() const override { return state_; }
    void set_fault_plan(const FaultPlan& plan) override { faults_ = plan; }

    Status connect(std::chrono::milliseconds timeout) override {
        if (state_ == LinkState::connected) return {};
        state_ = LinkState::connecting;
        if (faults_.connect_timeouts > 0) {
            --faults_.connect_timeouts;
            // A real probe that never answers costs the full timeout.
            if (latency_.count() > 0) std::this_thread::sleep_for(timeout);
            state_ = LinkState::fault;
            return {ErrorCode::timeout,
                    "[SIMULATED] " + info_.model + " did not answer within " + std::to_string(timeout.count()) +
                        " ms (injected timeout).",
                    true};
        }
        if (latency_.count() > 0) std::this_thread::sleep_for(latency_);
        state_ = LinkState::connected;
        sequence_ = 0;
        return {};
    }

    Status disconnect() override {
        state_ = LinkState::disconnected;
        return {};
    }

    Status poll_telemetry(TelemetryMessage& out) override {
        if (state_ != LinkState::connected)
            return {ErrorCode::not_connected, "[SIMULATED] " + info_.id + " is not connected.", false};
        if (faults_.drop_link_on_next_poll) {
            faults_.drop_link_on_next_poll = false;
            state_ = LinkState::fault;
            return {ErrorCode::link_lost, "[SIMULATED] USB link to " + info_.model + " lost (cable unplugged?).",
                    true};
        }
        out = TelemetryMessage{};
        out.device_id = info_.id;
        out.sequence = ++sequence_;
        out.device_time_ms = 1000ull * sequence_; // one sample per simulated second
        fill(out, static_cast<double>(sequence_));
        out.crc32 = crc32(out.canonical_payload());
        return {};
    }

protected:
    virtual void fill(TelemetryMessage& m, double t) const = 0;

    DeviceInfo info_;
    std::chrono::milliseconds latency_;
    std::atomic<LinkState> state_{LinkState::disconnected}; ///< atomic: read by the UI while a connect is in progress
    FaultPlan faults_;
    std::uint32_t sequence_ = 0;
};

class MockMcu final : public MockAdapterBase {
public:
    explicit MockMcu(std::chrono::milliseconds l)
        : MockAdapterBase({"mcu0", DeviceKind::microcontroller, "ST NUCLEO-F446RE (STM32F446RE, Cortex-M4F)",
                           "0483:374B (ST-LINK/V2-1)", "USB CDC-ACM virtual COM via ST-LINK, 115200 8N1",
                           "3.3 V I/O (most pins 5 V tolerant; analog pins are not)", "SIM-MCU-0001", true},
                          l) {}
    Capabilities capabilities() const override {
        return {{{"firmware.identify", "Reports firmware build id over the virtual COM port"},
                 {"telemetry.stream", "Periodic framed telemetry (CRC32) at 1 Hz"},
                 {"adc.read", "12-bit ADC sample of PA0 (0-3.3 V)"},
                 {"gpio.write", "Drive user LED LD2 (PA5)"},
                 {"spi.master", "SPI2 master to FPGA Pmod JA at 3.3 V (PB13 SCK, PB14 MISO, PB15 MOSI, PB12 CS)"}},
                true};
    }

protected:
    void fill(TelemetryMessage& m, double t) const override {
        m.fields["die_temp"] = 34.0 + 1.5 * std::sin(t / 7.0);
        m.units["die_temp"] = "C";
        m.fields["vdd"] = 3.30 - 0.004 * std::cos(t / 3.0);
        m.units["vdd"] = "V";
        m.fields["adc_pa0"] = 1.65 + 0.8 * std::sin(t / 2.0);
        m.units["adc_pa0"] = "V";
    }
};

class MockFpga final : public MockAdapterBase {
public:
    explicit MockFpga(std::chrono::milliseconds l)
        : MockAdapterBase({"fpga0", DeviceKind::fpga, "Digilent Arty A7-35T (Xilinx XC7A35T)",
                           "0403:6010 (FTDI FT2232HQ: ch.A JTAG, ch.B UART)",
                           "USB-UART on FT2232H channel B, 115200 8N1", "3.3 V on Pmod banks (VCCO)", "SIM-FPGA-0001",
                           true},
                          l) {}
    Capabilities capabilities() const override {
        return {{{"bitstream.status", "Reads DONE pin / config status (read-only in this build)"},
                 {"register.read", "Reads design status registers over the UART bridge"},
                 {"telemetry.stream", "XADC on-die temperature and supply rails"},
                 {"spi.slave", "SPI slave on Pmod JA, 3.3 V"}},
                true};
    }

protected:
    void fill(TelemetryMessage& m, double t) const override {
        m.fields["xadc_temp"] = 41.0 + 2.0 * std::sin(t / 11.0);
        m.units["xadc_temp"] = "C";
        m.fields["vccint"] = 1.000 + 0.003 * std::sin(t / 5.0);
        m.units["vccint"] = "V";
        m.fields["vccaux"] = 1.800 - 0.004 * std::cos(t / 6.0);
        m.units["vccaux"] = "V";
    }
};

class MockJtag final : public MockAdapterBase {
public:
    explicit MockJtag(std::chrono::milliseconds l)
        : MockAdapterBase({"jtag0", DeviceKind::jtag_probe, "SEGGER J-Link (JTAG/SWD debug probe)", "1366:0101 (J-Link)",
                           "USB bulk endpoints via vendor driver", "Follows target VTref (1.2-5 V sensed on pin 1)",
                           "SIM-JTAG-0001", true},
                          l) {}
    Capabilities capabilities() const override {
        // Deliberately no compute capability: a JTAG probe is a bridge from the
        // host to a target's Test Access Port, not a place to run workloads.
        return {{{"jtag.scan_chain", "Enumerates TAPs and IDCODEs on the connected chain"},
                 {"swd.debug", "Halt / step / resume an Arm Cortex-M core via SWD"},
                 {"memory.access", "Read/write target memory through the debug port"},
                 {"vtref.sense", "Measures target I/O voltage before driving any pin"},
                 {"flash.program", "Programs MCU flash (disabled in this simulated build)"}},
                false};
    }

protected:
    void fill(TelemetryMessage& m, double t) const override {
        m.fields["vtref"] = 3.29 + 0.005 * std::sin(t);
        m.units["vtref"] = "V";
        m.fields["tck_khz"] = 4000.0;
        m.units["tck_khz"] = "kHz";
        // Expected IDCODE of an Arm Cortex-M4 JTAG-DP, shown as an integer.
        m.fields["idcode_tap0"] = static_cast<double>(0x4BA00477u);
        m.units["idcode_tap0"] = "hex";
    }
};

} // namespace

std::unique_ptr<IDeviceAdapter> make_mock_mcu(std::chrono::milliseconds l) { return std::make_unique<MockMcu>(l); }
std::unique_ptr<IDeviceAdapter> make_mock_fpga(std::chrono::milliseconds l) { return std::make_unique<MockFpga>(l); }
std::unique_ptr<IDeviceAdapter> make_mock_jtag(std::chrono::milliseconds l) { return std::make_unique<MockJtag>(l); }

// ---------------------------------------------------------------- manager ----

DeviceManager::DeviceManager(std::chrono::milliseconds latency) {
    devices_.push_back({make_mock_mcu(latency), std::make_unique<std::mutex>()});
    devices_.push_back({make_mock_fpga(latency), std::make_unique<std::mutex>()});
    devices_.push_back({make_mock_jtag(latency), std::make_unique<std::mutex>()});
}

DeviceManager::Slot* DeviceManager::find(const std::string& id) {
    for (auto& d : devices_)
        if (d.adapter->info().id == id) return &d;
    return nullptr;
}

std::vector<DeviceInfo> DeviceManager::discover() {
    std::vector<DeviceInfo> out;
    for (auto& d : devices_) out.push_back(d.adapter->info());
    return out;
}

Capabilities DeviceManager::capabilities(const std::string& id) {
    Slot* s = find(id);
    return s ? s->adapter->capabilities() : Capabilities{};
}

ConnectOutcome DeviceManager::connect(const std::string& id, const RetryPolicy& policy) {
    ConnectOutcome out;
    Slot* s = find(id);
    if (!s) {
        out.final_status = {ErrorCode::not_found, "No device with id '" + id + "' on the bus.", false};
        return out;
    }
    std::lock_guard<std::mutex> lk(*s->mu);
    IDeviceAdapter* d = s->adapter.get();
    auto backoff = policy.initial_backoff;
    const int attempts = std::max(1, policy.max_attempts);
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        ConnectAttempt a;
        a.attempt = attempt;
        a.status = d->connect(policy.connect_timeout);
        const bool last = attempt == attempts;
        if (!a.status.ok() && a.status.retryable && !last) {
            a.backoff_ms = backoff.count();
            out.attempts.push_back(a);
            if (policy.sleep) std::this_thread::sleep_for(backoff);
            backoff *= 2; // exponential backoff: 100, 200, 400 ms ...
            continue;
        }
        out.attempts.push_back(a);
        out.final_status = a.status;
        break;
    }
    out.recovered = out.final_status.ok() && out.attempts.size() > 1;
    if (!out.final_status.ok() && d->state() == LinkState::fault) {
        // Leave the adapter in a clean, retryable state rather than "fault".
        d->disconnect();
        out.final_status.message += " Gave up after " + std::to_string(out.attempts.size()) +
                                    " attempt(s); the device was reset to 'disconnected' and can be retried.";
    }
    return out;
}

Status DeviceManager::disconnect(const std::string& id) {
    Slot* s = find(id);
    if (!s) return {ErrorCode::not_found, "Unknown device '" + id + "'.", false};
    std::lock_guard<std::mutex> lk(*s->mu);
    return s->adapter->disconnect();
}

Status DeviceManager::poll(const std::string& id, TelemetryMessage& out) {
    Slot* s = find(id);
    if (!s) return {ErrorCode::not_found, "Unknown device '" + id + "'.", false};
    std::lock_guard<std::mutex> lk(*s->mu);
    return s->adapter->poll_telemetry(out);
}

LinkState DeviceManager::state(const std::string& id) {
    Slot* s = find(id);
    return s ? s->adapter->state() : LinkState::disconnected; // lock-free read, see state_
}

void DeviceManager::inject(const std::string& id, const FaultPlan& plan) {
    if (Slot* s = find(id)) {
        std::lock_guard<std::mutex> lk(*s->mu);
        s->adapter->set_fault_plan(plan);
    }
}

} // namespace wb::hw
