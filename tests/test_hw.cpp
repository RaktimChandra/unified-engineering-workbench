#include <doctest/doctest.h>

#include "wb/hw/device.hpp"

using namespace wb::hw;
using std::chrono::milliseconds;

namespace {
RetryPolicy fast() {
    RetryPolicy p;
    p.sleep = false;
    return p;
}
} // namespace

TEST_SUITE("hardware_mocks") {

TEST_CASE("CRC32 matches the IEEE check value") { CHECK(crc32("123456789") == 0xCBF43926u); }

TEST_CASE("discovery reports all three device kinds, all labelled simulated") {
    DeviceManager m(milliseconds(0));
    const auto devs = m.discover();
    REQUIRE(devs.size() == 3);
    CHECK(devs[0].kind == DeviceKind::microcontroller);
    CHECK(devs[1].kind == DeviceKind::fpga);
    CHECK(devs[2].kind == DeviceKind::jtag_probe);
    for (const auto& d : devs) {
        CHECK(d.simulated);
        CHECK_FALSE(d.usb_vid_pid.empty());
    }
}

TEST_CASE("a JTAG probe is a debug interface, not a compute board") {
    DeviceManager m(milliseconds(0));
    CHECK_FALSE(m.capabilities("jtag0").general_compute);
    CHECK(m.capabilities("mcu0").general_compute);
    bool has_scan = false;
    for (const auto& c : m.capabilities("jtag0").items) has_scan |= c.name == "jtag.scan_chain";
    CHECK(has_scan);
}

TEST_CASE("connect, telemetry with valid CRC and increasing sequence, disconnect") {
    DeviceManager m(milliseconds(0));
    for (const char* id : {"mcu0", "fpga0", "jtag0"}) {
        CAPTURE(id);
        const auto o = m.connect(id, fast());
        REQUIRE(o.final_status.ok());
        CHECK_FALSE(o.recovered);
        CHECK(m.state(id) == LinkState::connected);
        TelemetryMessage a, b;
        REQUIRE(m.poll(id, a).ok());
        REQUIRE(m.poll(id, b).ok());
        CHECK(b.sequence == a.sequence + 1);
        CHECK(a.simulated);
        CHECK(a.crc32 == crc32(a.canonical_payload()));
        CHECK_FALSE(a.fields.empty());
        CHECK(m.disconnect(id).ok());
        CHECK(m.state(id) == LinkState::disconnected);
        CHECK(m.poll(id, a).code == ErrorCode::not_connected);
    }
}

TEST_CASE("recoverable failure: injected timeouts are retried with exponential backoff") {
    DeviceManager m(milliseconds(0));
    m.inject("fpga0", {2, false});
    const auto o = m.connect("fpga0", fast());
    CHECK(o.final_status.ok());
    CHECK(o.recovered);
    REQUIRE(o.attempts.size() == 3);
    CHECK(o.attempts[0].status.code == ErrorCode::timeout);
    CHECK(o.attempts[0].backoff_ms == 100);
    CHECK(o.attempts[1].backoff_ms == 200);
    CHECK(o.attempts[2].status.ok());
}

TEST_CASE("persistent failure gives up cleanly and stays retryable") {
    DeviceManager m(milliseconds(0));
    m.inject("mcu0", {5, false});
    const auto o = m.connect("mcu0", fast());
    CHECK_FALSE(o.final_status.ok());
    CHECK(o.final_status.code == ErrorCode::timeout);
    CHECK(o.attempts.size() == 3);
    CHECK(m.state("mcu0") == LinkState::disconnected); // not stuck in 'fault'
    // Two timeouts remain armed; the next 3-attempt connect consumes them and succeeds.
    CHECK(m.connect("mcu0", fast()).recovered);
}

TEST_CASE("link loss during streaming is reported and recovered by reconnecting") {
    DeviceManager m(milliseconds(0));
    REQUIRE(m.connect("jtag0", fast()).final_status.ok());
    m.inject("jtag0", {0, true});
    TelemetryMessage msg;
    const auto s = m.poll("jtag0", msg);
    CHECK(s.code == ErrorCode::link_lost);
    CHECK(s.retryable);
    CHECK(m.state("jtag0") == LinkState::fault);
    REQUIRE(m.connect("jtag0", fast()).final_status.ok());
    CHECK(m.poll("jtag0", msg).ok());
    CHECK(msg.sequence == 1); // sequence restarts on a new session
}

TEST_CASE("unknown device ids are reported, not crashed on") {
    DeviceManager m(milliseconds(0));
    CHECK(m.connect("nope", fast()).final_status.code == ErrorCode::not_found);
    TelemetryMessage msg;
    CHECK(m.poll("nope", msg).code == ErrorCode::not_found);
}

} // TEST_SUITE
