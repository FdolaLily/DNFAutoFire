#pragma once
// Small Win32 file helpers shared by the client, the service and the migration:
// bounded whole-file reads, atomic replace-on-write and cheap change stamps.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace dafclient::fs {

struct Stamp {
    bool exists = false;
    unsigned long long size = 0, writeTime = 0;
    bool operator==(const Stamp& o) const { return exists == o.exists && size == o.size && writeTime == o.writeTime; }
    bool operator!=(const Stamp& o) const { return !(*this == o); }
};
Stamp stamp(const std::wstring& path);          // One metadata call; never opens the file.
bool exists(const std::wstring& path);
bool isFile(const std::wstring& path);
// Reads the whole file; throws std::runtime_error when missing, unreadable or larger than maxBytes.
std::string read(const std::wstring& path, size_t maxBytes);
// Writes to a unique sibling temporary file, flushes, then atomically replaces the target.
void writeAtomic(const std::wstring& path, const std::string& bytes);
bool remove(const std::wstring& path);          // True when the file is gone afterwards.

std::wstring modulePath();                      // Full path of this EXE.
std::wstring directoryOf(const std::wstring& path);
std::wstring fileName(const std::wstring& path);
std::wstring join(const std::wstring& directory, const std::wstring& name);
std::wstring fullPath(const std::wstring& path);
bool samePath(const std::wstring& a, const std::wstring& b); // Case-insensitive full-path comparison.

// Machine-wide lock serialising config.json writers (client, service, migration).
// Falls back to a session-local lock when the global namespace is not available.
class ConfigLock {
public:
    ConfigLock();
    ~ConfigLock();
    ConfigLock(const ConfigLock&) = delete;
    ConfigLock& operator=(const ConfigLock&) = delete;
private:
    HANDLE mutex_ = nullptr;
    bool owned_ = false;
};

// SDDL granting SYSTEM and Administrators full access and authenticated users
// SYNCHRONIZE, so the LocalSystem service and the elevated client share objects.
bool sharedSecurity(SECURITY_ATTRIBUTES& attributes, bool allowUsersSync = true);
void freeSharedSecurity(SECURITY_ATTRIBUTES& attributes);

} // namespace dafclient::fs
