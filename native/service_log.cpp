#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "service_log.h"
#include "json.h"
#include <cstdio>

namespace dafclient::svc {

Log::Log(std::wstring path, unsigned long long limitBytes) : path_(std::move(path)), limit_(limitBytes) {}
Log::~Log() { if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }

bool Log::open() {
    if (file_ != INVALID_HANDLE_VALUE) return true;
    const auto slash = path_.find_last_of(L"\\/");
    if (slash != std::wstring::npos) CreateDirectoryW(path_.substr(0, slash).c_str(), nullptr);
    file_ = CreateFileW(path_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file_ == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    size_ = GetFileSizeEx(file_, &size) ? static_cast<unsigned long long>(size.QuadPart) : 0;
    if (size_ == 0) { DWORD written = 0; WriteFile(file_, "\xef\xbb\xbf", 3, &written, nullptr); size_ = written; }
    return true;
}
void Log::roll() {
    if (file_ != INVALID_HANDLE_VALUE) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
    // One previous file is kept; anything older is replaced.
    if (!MoveFileExW(path_.c_str(), (path_ + L".1").c_str(), MOVEFILE_REPLACE_EXISTING)) DeleteFileW(path_.c_str());
    size_ = 0;
}
void Log::write(const wchar_t* level, const std::wstring& message) {
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t stamp[48];
    swprintf_s(stamp, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, level);
    std::wstring line = stamp + message;
    for (auto& c : line) if (c == L'\r' || c == L'\n') c = L' ';
    std::string bytes;
    try { bytes = json::narrow(line) + "\r\n"; } catch (...) { return; }
    AcquireSRWLockExclusive(&lock_);
    if (open() && size_ + bytes.size() > limit_ && size_ > 3) { roll(); open(); }
    if (file_ != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        if (WriteFile(file_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr)) size_ += written;
    }
    ReleaseSRWLockExclusive(&lock_);
}
std::wstring Log::errorText(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = std::to_wstring(code);
    if (n && buffer) {
        std::wstring detail(buffer, n);
        while (!detail.empty() && (detail.back() == L'\r' || detail.back() == L'\n' || detail.back() == L' ')) detail.pop_back();
        text += L" " + detail;
    }
    if (buffer) LocalFree(buffer);
    return text;
}

} // namespace dafclient::svc
