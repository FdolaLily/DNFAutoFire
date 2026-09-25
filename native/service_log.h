#pragma once
// Size-bounded UTF-8 log for the service: logs\service.log plus at most one
// previous file (service.log.1), rolled at a fixed size and shareable for reading.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>

namespace dafclient::svc {

class Log {
public:
    explicit Log(std::wstring path, unsigned long long limitBytes = 128 * 1024);
    ~Log();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;
    void info(const std::wstring& message) { write(L"INF", message); }
    void warn(const std::wstring& message) { write(L"WRN", message); }
    void error(const std::wstring& message) { write(L"ERR", message); }
    void write(const wchar_t* level, const std::wstring& message);
    const std::wstring& path() const { return path_; }
    static std::wstring errorText(DWORD code); // "5 拒绝访问。" style system message.
private:
    bool open();
    void roll();
    std::wstring path_;
    unsigned long long limit_, size_ = 0;
    HANDLE file_ = INVALID_HANDLE_VALUE;
    SRWLOCK lock_ = SRWLOCK_INIT;
};

} // namespace dafclient::svc
