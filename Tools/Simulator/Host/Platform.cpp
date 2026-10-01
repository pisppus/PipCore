#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <malloc.h>
#endif

#ifndef PIPCORE_SIM_DEFAULT_WIDTH
#define PIPCORE_SIM_DEFAULT_WIDTH 480
#endif
#ifndef PIPCORE_SIM_DEFAULT_HEIGHT
#define PIPCORE_SIM_DEFAULT_HEIGHT 320
#endif

#include "Host/Platform.hpp"
#include "Host/Runtime.hpp"
#include <Debug.hpp>

uint64_t pipcore::debug::profileCycles() noexcept
{
    const auto now = std::chrono::high_resolution_clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
}

namespace pipcore::debug::detail
{
    namespace
    {
        std::atomic<bool> &profilerSpin() noexcept
        {
            static std::atomic<bool> flag{false};
            return flag;
        }
    }

    void profilerLock() noexcept
    {
        while (profilerSpin().exchange(true, std::memory_order_acquire))
        {
            while (profilerSpin().load(std::memory_order_relaxed))
                std::this_thread::yield();
        }
    }

    void profilerUnlock() noexcept
    {
        profilerSpin().store(false, std::memory_order_release);
    }
}

namespace pipcore::desktop
{
    uint32_t Platform::nowMs() noexcept
    {
        return Runtime::instance().nowMs();
    }

    uint64_t Platform::nowUs() noexcept
    {
        return Runtime::instance().nowMicros();
    }

    void Platform::delayMs(uint32_t ms) noexcept
    {
        Runtime::instance().delayMs(ms);
    }

    bool Platform::shouldQuit() const noexcept
    {
        return Runtime::instance().shouldQuit();
    }

    void Platform::pinModeInput(uint8_t pin, InputMode mode) noexcept
    {
        Runtime::instance().pinModeInput(pin, mode);
    }

    bool Platform::digitalRead(uint8_t pin) noexcept
    {
        return Runtime::instance().digitalRead(pin);
    }

    int16_t Platform::analogRead(uint8_t pin) noexcept
    {
        return Runtime::instance().analogRead(pin);
    }

    void *Platform::alloc(size_t bytes, AllocCaps) noexcept
    {
        if (bytes == 0)
            return nullptr;
        void *ptr = std::malloc(bytes);
        if (ptr)
            Runtime::instance().simHeapTrackAdd(ptr, bytes);
        return ptr;
    }

    void Platform::free(void *ptr) noexcept
    {
        if (!ptr)
            return;
        (void)Runtime::instance().simHeapTrackRemove(ptr);
        std::free(ptr);
    }

    void *Platform::allocAligned(size_t bytes, size_t align, AllocCaps) noexcept
    {
        if (bytes == 0)
            return nullptr;

#if defined(_MSC_VER) || defined(__MINGW32__)
        void *ptr = _aligned_malloc(bytes, align);
#else
        void *ptr = nullptr;
        if (align < sizeof(void *))
        {
            align = sizeof(void *);
        }
        if (posix_memalign(&ptr, align, bytes) != 0)
        {
            ptr = nullptr;
        }
#endif
        if (ptr)
            Runtime::instance().simHeapTrackAdd(ptr, bytes);
        return ptr;
    }

    void Platform::freeAligned(void *ptr) noexcept
    {
        if (!ptr)
            return;
        (void)Runtime::instance().simHeapTrackRemove(ptr);

#if defined(_MSC_VER) || defined(__MINGW32__)
        _aligned_free(ptr);
#else
        std::free(ptr);
#endif
    }

    bool Platform::configDisplay(const DisplayConfig &cfg) noexcept
    {
        if (cfg.width == 0 || cfg.height == 0)
            return false;

        _config = cfg;
        _configValid = _display.configure(cfg.width, cfg.height);
        return _configValid;
    }

    bool Platform::beginDisplay(uint8_t rotation) noexcept
    {
        if (!_configValid)
        {
            DisplayConfig fallback = {};
            fallback.width = static_cast<uint16_t>(PIPCORE_SIM_DEFAULT_WIDTH);
            fallback.height = static_cast<uint16_t>(PIPCORE_SIM_DEFAULT_HEIGHT);
            if (!configDisplay(fallback))
                return false;
        }
        return _display.begin(rotation);
    }

    bool Platform::setDisplayRotation(uint8_t rotation) noexcept
    {
        return _display.setRotation(rotation);
    }

    void Platform::WifiBackend::request(bool enabled) noexcept
    {
        _state = enabled ? pipcore::net::WifiState::Unsupported : pipcore::net::WifiState::Off;
    }

    void Platform::OtaBackend::markAppValid() noexcept
    {
        _status.pendingVerify = false;
        notify();
    }

    void Platform::OtaBackend::configure(const pipcore::ota::Options &opt, pipcore::ota::StatusCallback cb,
                                         void *user) noexcept
    {
        _options = opt;
        _callback = cb;
        _callbackUser = user;
        _status = {};
        _status.state = pipcore::ota::State::Idle;
        _status.lastChangeMs = Runtime::instance().nowMs();
        notify();
    }

    void Platform::OtaBackend::requestCheck(pipcore::ota::CheckMode) noexcept
    {
        fail(pipcore::ota::Error::WifiNotEnabled);
    }

    void Platform::OtaBackend::requestInstall() noexcept
    {
        fail(pipcore::ota::Error::WifiNotEnabled);
    }

    void Platform::OtaBackend::requestStableList() noexcept
    {
        fail(pipcore::ota::Error::WifiNotEnabled);
    }

    const char *Platform::OtaBackend::stableListVersion(uint8_t) const noexcept
    {
        return "";
    }

    void Platform::OtaBackend::requestInstallStableVersion(const char *) noexcept
    {
        fail(pipcore::ota::Error::WifiNotEnabled);
    }

    void Platform::OtaBackend::cancel() noexcept
    {
        _status.state = pipcore::ota::State::Idle;
        _status.error = pipcore::ota::Error::None;
        _status.lastChangeMs = Runtime::instance().nowMs();
        notify();
    }

    void Platform::OtaBackend::fail(pipcore::ota::Error error) noexcept
    {
        _status.state = pipcore::ota::State::Error;
        _status.error = error;
        _status.lastChangeMs = Runtime::instance().nowMs();
        notify();
    }

    void Platform::OtaBackend::notify() noexcept
    {
        if (_callback)
            _callback(_status, _callbackUser);
    }
}

namespace pipcore
{

    Platform *GetPlatform() noexcept
    {
        static desktop::Platform instance;
        return &instance;
    }
}

#endif
