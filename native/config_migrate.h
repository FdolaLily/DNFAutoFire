#pragma once
// One-time import of the legacy configuration files into config.json:
//   config.ini                   (DNFAutoFire, UTF-16LE / UTF-8 / ANSI INI)
//   appsettings.json             (former DNFProcessManager service, .NET JSON with comments)
//   appsettings.Development.json (former service logging override, no longer used)
// A legacy file is removed only after config.json has been written atomically and
// re-read successfully. A file whose content config.json already holds is left alone.
#include <string>
#include <vector>
#include "client_config.h"
#include "json.h"

namespace dafclient {

constexpr wchar_t kLegacyIniName[] = L"config.ini";
constexpr wchar_t kLegacyServiceJsonName[] = L"appsettings.json";
constexpr wchar_t kLegacyServiceDevJsonName[] = L"appsettings.Development.json";

struct MigrationReport {
    std::vector<std::wstring> imported;  // Legacy files whose content is now in config.json.
    std::vector<std::wstring> removed;   // Legacy files deleted afterwards.
    std::vector<std::wstring> kept;      // Legacy files left in place (already superseded or not deletable).
    std::wstring error;                  // Files that could not be imported (left unchanged), with reasons.
    bool migrated() const { return !imported.empty(); }
};

// Safe to call on every start from both the client and the service: it returns
// immediately when no legacy file exists and serialises concurrent callers.
MigrationReport migrateLegacyConfig(const std::wstring& directory);

// Pure conversions, exposed for tests.
std::wstring decodeLegacyText(const std::string& bytes);
// Imports settings, profiles and unknown INI content ("legacyIni") into root.
void importLegacyIni(json::Value& root, const std::wstring& iniText);
// Returns service options from an appsettings.json "Manager" section; throws on invalid JSON.
ServiceOptions importAppSettings(const std::string& utf8);

} // namespace dafclient
