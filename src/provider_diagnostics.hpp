#pragma once
#include "config_store.hpp"

namespace sat {
// Read-only local config checks; online=true is an explicit, single-POST probe.
// The loopback allowance is used only by the separately compiled test executable.
json::Value providerDiagnostic(const Config& config,bool online,bool rewrite=false,bool allowLoopbackForTests=false);
int runProviderDiagnostic(bool online,bool rewrite=false,bool allowLoopbackForTests=false);
}
