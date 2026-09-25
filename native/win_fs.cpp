#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <sddl.h>
#include "win_fs.h"
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace dafclient::fs {
namespace {
constexpr wchar_t kConfigLock[] = L"DNFAutoFire.Config.{5C1D6E1B-8A0E-4F7B-9C4D-3E2A61B0D7F4}";
std::wstring lower(std::wstring s) { for (auto& c : s) c = wchar_t(towlower(c)); return s; }
}

Stamp stamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    Stamp result;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return result;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return result;
    result.exists = true;
    result.size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    result.writeTime = (static_cast<unsigned long long>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    return result;
}
bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool isFile(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
std::string read(const std::wstring& path, size_t maxBytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot open file for reading.");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > maxBytes) {
        CloseHandle(file); throw std::runtime_error("File is too large or its size is unavailable.");
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD got = 0;
        const DWORD want = static_cast<DWORD>(std::min<size_t>(bytes.size() - done, 1u << 20));
        if (!ReadFile(file, bytes.data() + done, want, &got, nullptr) || !got) { CloseHandle(file); throw std::runtime_error("Cannot read file."); }
        done += got;
    }
    CloseHandle(file);
    return bytes;
}
void writeAtomic(const std::wstring& path, const std::string& bytes) {
    const auto temporary = path + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetCurrentThreadId())
        + L"." + std::to_wstring(GetTickCount64());
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot create configuration transaction.");
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)
        && written == bytes.size() && FlushFileBuffers(file);
    CloseHandle(file);
    if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Cannot commit configuration transaction.");
    }
}
bool remove(const std::wstring& path) {
    if (DeleteFileW(path.c_str())) return true;
    DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) {
        // A read-only legacy file (e.g. extracted from an archive) is still ours to retire.
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) &&
            SetFileAttributesW(path.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY)) {
            if (DeleteFileW(path.c_str())) return true;
            error = GetLastError();
            SetFileAttributesW(path.c_str(), attributes);
        }
    }
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}

std::wstring modulePath() {
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) throw std::runtime_error("GetModuleFileName failed");
    path.resize(count);
    return path;
}
std::wstring directoryOf(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}
std::wstring fileName(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}
std::wstring join(const std::wstring& directory, const std::wstring& name) {
    if (directory.empty()) return name;
    const wchar_t last = directory.back();
    return last == L'\\' || last == L'/' ? directory + name : directory + L"\\" + name;
}
std::wstring fullPath(const std::wstring& path) {
    const DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!needed) return path;
    std::wstring result(needed, L'\0');
    const DWORD count = GetFullPathNameW(path.c_str(), needed, result.data(), nullptr);
    if (!count || count >= needed) return path;
    result.resize(count);
    return result;
}
bool samePath(const std::wstring& a, const std::wstring& b) {
    if (a.empty() || b.empty()) return false;
    return lower(fullPath(a)) == lower(fullPath(b));
}

bool sharedSecurity(SECURITY_ATTRIBUTES& attributes, bool allowUsersSync) {
    attributes = {}; attributes.nLength = sizeof(attributes);
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    const wchar_t* sddl = allowUsersSync ? L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x00100000;;;AU)" : L"D:(A;;GA;;;SY)(A;;GA;;;BA)";
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &descriptor, nullptr)) return false;
    attributes.lpSecurityDescriptor = descriptor;
    return true;
}
void freeSharedSecurity(SECURITY_ATTRIBUTES& attributes) {
    if (attributes.lpSecurityDescriptor) LocalFree(attributes.lpSecurityDescriptor);
    attributes.lpSecurityDescriptor = nullptr;
}

ConfigLock::ConfigLock() {
    SECURITY_ATTRIBUTES security{};
    const bool secured = sharedSecurity(security);
    mutex_ = CreateMutexExW(secured ? &security : nullptr, (std::wstring(L"Global\\") + kConfigLock).c_str(), 0, SYNCHRONIZE);
    freeSharedSecurity(security);
    if (!mutex_) mutex_ = CreateMutexExW(nullptr, (std::wstring(L"Local\\") + kConfigLock).c_str(), 0, SYNCHRONIZE);
    if (!mutex_) return; // Best effort: writes stay atomic even without cross-process ordering.
    const DWORD wait = WaitForSingleObject(mutex_, 10000);
    owned_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
}
ConfigLock::~ConfigLock() {
    if (owned_) ReleaseMutex(mutex_);
    if (mutex_) CloseHandle(mutex_);
}

} // namespace dafclient::fs
