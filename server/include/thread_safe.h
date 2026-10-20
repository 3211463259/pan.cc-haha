#pragma once

// MinGW 8.1 win32 线程模型不支持 std::mutex
// 使用 Win32 CRITICAL_SECTION 替代

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace cloud {

class Mutex {
public:
    Mutex() { InitializeCriticalSection(&cs_); }
    ~Mutex() { DeleteCriticalSection(&cs_); }
    void lock() { EnterCriticalSection(&cs_); }
    void unlock() { LeaveCriticalSection(&cs_); }
    bool try_lock() { return TryEnterCriticalSection(&cs_); }
private:
    CRITICAL_SECTION cs_;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
};

template<typename T>
class LockGuard {
public:
    explicit LockGuard(T& mtx) : mtx_(mtx) { mtx_.lock(); }
    ~LockGuard() { mtx_.unlock(); }
private:
    T& mtx_;
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
};

} // namespace cloud
