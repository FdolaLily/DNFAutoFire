#pragma once
// config.json schema writers shared by Store and the one-time legacy migration.
// Each writer updates known members in place and leaves unknown members untouched.
#include "client_config.h"
#include "json.h"

namespace dafclient::schema {
void writeSettings(json::Value& root, const Settings& settings);
// profile must be a JSON object (or null); its "name" member is set from the model.
void writeProfile(json::Value& profile, const Profile& model);
// Also persists an explicit per-profile direction override while it is disabled.
void writeDormantRunKeys(json::Value& profile, const std::array<std::wstring, 4>& keys);
void writeService(json::Value& root, const ServiceOptions& options);
// Clamps and cleans values exactly as Store::loadService does.
ServiceOptions normalizeService(ServiceOptions options);
} // namespace dafclient::schema
