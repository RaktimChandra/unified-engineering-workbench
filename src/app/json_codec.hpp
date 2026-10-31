// Conversions between wb_core value types and JSON. Kept out of the core so
// the numerical library stays transport-agnostic.
#pragma once

#include "wb/backprop.hpp"
#include "wb/benchmark.hpp"
#include "wb/common.hpp"
#include "wb/fea1d.hpp"
#include "wb/hw/device.hpp"
#include "wb/nodal_csv.hpp"
#include "wb/stl.hpp"
#include "wb/sysinfo.hpp"

#include <nlohmann/json.hpp>

namespace wb::app {

using nlohmann::json;

json to_json(const Diagnostic& d);
json to_json(const std::vector<Diagnostic>& v);
json to_json(const stl::MeshStats& s);
json to_json(const sys::HostInfo& h);
json to_json(const bench::Report& r);
json to_json(const fea::ErrorReport& e);
json to_json(const hw::DeviceInfo& d);
json to_json(const hw::Capabilities& c);
json to_json(const hw::Status& s);
json to_json(const hw::TelemetryMessage& m);
json to_json(const hw::ConnectOutcome& o);
json to_json(const bp::Params& p);
json to_json(const bp::Gradients& g);

/// Reads a RodProblem from JSON, keeping Task C defaults for absent fields.
/// Throws std::invalid_argument with a field-specific message for wrong types.
fea::RodProblem rod_from_json(const json& j);
json to_json(const fea::RodProblem& p);

} // namespace wb::app
