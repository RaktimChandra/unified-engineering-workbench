// Integration tests: real HTTP server on 127.0.0.1, real client, same routes
// the browser uses. Verifies transport, job lifecycle, cancellation and errors.
#include <doctest/doctest.h>

#include "api.hpp"
#include "test_support.hpp"

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <thread>

using nlohmann::json;
using namespace std::chrono_literals;

namespace {

struct Fixture {
    wb::app::Api api{[] {
        wb::app::ApiConfig c;
        c.data_dir = wb::test::data_dir();
        c.device_latency = std::chrono::milliseconds(0);
        return c;
    }()};
    httplib::Server srv;
    std::thread th;
    int port = 0;
    std::unique_ptr<httplib::Client> cli;

    Fixture() {
        api.register_routes(srv);
        port = srv.bind_to_any_port("127.0.0.1");
        th = std::thread([this] { srv.listen_after_bind(); });
        cli = std::make_unique<httplib::Client>("127.0.0.1", port);
        cli->set_read_timeout(30, 0);
    }
    ~Fixture() {
        srv.stop();
        th.join();
    }
    json wait_job(const std::string& id) {
        for (int i = 0; i < 2000; ++i) {
            auto r = cli->Get("/api/jobs/" + id);
            REQUIRE(r);
            json j = json::parse(r->body);
            const auto st = j["state"].get<std::string>();
            if (st != "queued" && st != "running") return j;
            std::this_thread::sleep_for(5ms);
        }
        FAIL("job did not finish");
        return {};
    }
};

} // namespace

TEST_SUITE("api") {

TEST_CASE("health and system endpoints") {
    Fixture f;
    auto r = f.cli->Get("/api/health");
    REQUIRE(r);
    CHECK(r->status == 200);
    CHECK(json::parse(r->body)["ok"] == true);
    auto s = f.cli->Get("/api/system");
    REQUIRE(s);
    CHECK(json::parse(s->body)["host"]["logical_cpus"].get<int>() >= 1);
}

TEST_CASE("geometry import of the bundled example returns stats and a blob") {
    Fixture f;
    auto r = f.cli->Post("/api/geometry/import?example=heatsink_fins.stl&units=mm", "", "application/octet-stream");
    REQUIRE(r);
    CHECK(r->status == 202);
    const json j = f.wait_job(json::parse(r->body)["job_id"]);
    REQUIRE(j["state"] == "done");
    const auto tris = j["result"]["stats"]["triangles"].get<std::size_t>();
    CHECK(j["result"]["stats"]["closed"] == true);
    auto blob = f.cli->Get("/api/jobs/" + j["id"].get<std::string>() + "/blob");
    REQUIRE(blob);
    CHECK(blob->body.size() == tris * 12 * sizeof(float));
}

TEST_CASE("malformed upload fails with line-numbered diagnostic") {
    Fixture f;
    auto r = f.cli->Post("/api/geometry/import?name=bad.stl", "solid a\nfacet normal 0 0 1\nouter loop\nvertex 1 2 nan\n",
                         "application/octet-stream");
    REQUIRE(r);
    const json j = f.wait_job(json::parse(r->body)["job_id"]);
    CHECK(j["state"] == "failed");
    CHECK(j["error"]["code"] == "stl.non_finite");
    CHECK(j["error"]["line"] == 4);
    auto blob = f.cli->Get("/api/jobs/" + j["id"].get<std::string>() + "/blob");
    CHECK(blob->status == 404); // no partial result published
}

TEST_CASE("path traversal in example names is refused") {
    Fixture f;
    auto r = f.cli->Post("/api/geometry/import?example=../CMakeLists.txt", "", "application/octet-stream");
    REQUIRE(r);
    CHECK(r->status == 400);
}

TEST_CASE("nodal import and exact node inspection") {
    Fixture f;
    auto r = f.cli->Post("/api/results/import?name=t.csv",
                         "node_id,x_m,y_m,z_m,temperature_C\n10,0,0,0,20.123456789\n11,1,0,0,30\n", "application/octet-stream");
    const json j = f.wait_job(json::parse(r->body)["job_id"]);
    REQUIRE(j["state"] == "done");
    auto n = f.cli->Get("/api/results/" + j["id"].get<std::string>() + "/node/0");
    REQUIRE(n);
    const json node = json::parse(n->body);
    CHECK(node["node_id"] == 10);
    CHECK(node["temperature_C"].get<double>() == 20.123456789);
    CHECK(f.cli->Get("/api/results/" + j["id"].get<std::string>() + "/node/5")->status == 404);
}

TEST_CASE("FEA solve validates input and returns the assembly for small meshes") {
    Fixture f;
    auto r = f.cli->Post("/api/fea/solve", R"({"elements": 4})", "application/json");
    REQUIRE(r);
    REQUIRE(r->status == 200);
    const json j = json::parse(r->body);
    CHECK(j["T"].size() == 5);
    CHECK(j.contains("assembly"));
    auto bad = f.cli->Post("/api/fea/solve", R"({"problem": {"conductivity": -5}, "elements": 4})", "application/json");
    CHECK(bad->status == 400);
    CHECK(json::parse(bad->body)["error"]["code"] == "invalid_problem");
    auto badtype = f.cli->Post("/api/fea/solve", R"({"problem": {"length": "one"}})", "application/json");
    CHECK(badtype->status == 400);
    auto notjson = f.cli->Post("/api/fea/solve", "{oops", "application/json");
    CHECK(notjson->status == 400);
}

TEST_CASE("FEA study job exposes CSV exports identical to the CLI output") {
    Fixture f;
    auto r = f.cli->Post("/api/fea/study", "{}", "application/json");
    const json j = f.wait_job(json::parse(r->body)["job_id"]);
    REQUIRE(j["state"] == "done");
    CHECK(j["result"]["rows"].size() == 4);
    auto csv = f.cli->Get("/api/jobs/" + j["id"].get<std::string>() + "/export/fea_convergence.csv");
    REQUIRE(csv);
    CHECK(csv->status == 200);
    CHECK(csv->body.rfind("elements,h_m", 0) == 0);
}

TEST_CASE("benchmark job can be cancelled and publishes no result") {
    Fixture f;
    auto r = f.cli->Post("/api/bench/run", R"({"cases": 20000, "elements": 2000, "measured_runs": 5})", "application/json");
    REQUIRE(r->status == 202);
    const std::string id = json::parse(r->body)["job_id"];
    std::this_thread::sleep_for(50ms);
    CHECK(f.cli->Post("/api/jobs/" + id + "/cancel", "", "text/plain")->status == 200);
    const json j = f.wait_job(id);
    CHECK(j["state"] == "cancelled");
    CHECK_FALSE(j.contains("result"));
    // The server stays responsive and can run a new small benchmark afterwards.
    auto r2 = f.cli->Post("/api/bench/run", R"({"cases": 32, "elements": 32, "measured_runs": 1, "workers": [1,2]})",
                          "application/json");
    const json j2 = f.wait_job(json::parse(r2->body)["job_id"]);
    CHECK(j2["state"] == "done");
    CHECK(j2["result"]["agreement_ok"] == true);
}

TEST_CASE("benchmark rejects oversized workloads before running") {
    Fixture f;
    auto r = f.cli->Post("/api/bench/run", R"({"cases": 200000, "elements": 100000})", "application/json");
    CHECK(r->status == 400);
}

TEST_CASE("hardware: timeout injection then recovery over HTTP") {
    Fixture f;
    auto d = f.cli->Get("/api/hw/devices");
    CHECK(json::parse(d->body)["devices"].size() == 3);
    f.cli->Post("/api/hw/jtag0/fault", R"({"connect_timeouts": 1})", "application/json");
    auto c = f.cli->Post("/api/hw/jtag0/connect", "", "text/plain");
    const json j = f.wait_job(json::parse(c->body)["job_id"]);
    REQUIRE(j["state"] == "done");
    CHECK(j["result"]["recovered"] == true);
    auto t = f.cli->Get("/api/hw/jtag0/telemetry");
    CHECK(t->status == 200);
    CHECK(json::parse(t->body)["message"]["crc_ok"] == true);
    f.cli->Post("/api/hw/jtag0/disconnect", "", "text/plain");
    CHECK(f.cli->Get("/api/hw/jtag0/telemetry")->status == 409);
}

TEST_CASE("backprop and self-test jobs") {
    Fixture f;
    auto b = f.cli->Post("/api/backprop/run", "{}", "application/json");
    const json jb = f.wait_job(json::parse(b->body)["job_id"]);
    REQUIRE(jb["state"] == "done");
    CHECK(jb["result"]["loss"].size() == 101);
    auto s = f.cli->Post("/api/selftest", "", "text/plain");
    const json js = f.wait_job(json::parse(s->body)["job_id"]);
    REQUIRE(js["state"] == "done");
    for (const auto& c : js["result"]["checks"]) {
        CAPTURE(c.dump());
        CHECK(c["pass"] == true);
    }
}

} // TEST_SUITE
