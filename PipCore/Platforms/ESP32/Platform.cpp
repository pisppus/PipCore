#include <cstddef>
#include <cstdlib>
#include <new>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "Platforms/ESP32/Platform.hpp"
#if PIPCORE_ENABLE_DEBUG
#include "Platforms/ESP32/Core/Alloc.hpp"
#endif
#if PIPCORE_DEBUG_CONSOLE
#include "Platforms/ESP32/Debug/Console.hpp"
#endif

namespace pipcore::esp32
{
    Platform::Platform()
#if PIPCORE_ENABLE_AUDIO
        : _audio(static_cast<pipcore::audio::Backend &>(_audioBackend))
#endif
    {
#if PIPCORE_ENABLE_OTA
        _ota.bindWifi(&_wifi);
#endif
#if PIPCORE_DEBUG_CONSOLE
        pipcore::debug::Console::instance().begin();
#endif
    }

    void Platform::pinModeInput(uint8_t pin, InputMode mode) noexcept
    {
        _gpio.pinModeInput(pin, mode);
    }

    bool Platform::digitalRead(uint8_t pin) noexcept
    {
        return _gpio.digitalRead(pin);
    }

    int16_t Platform::analogRead(uint8_t pin) noexcept
    {
        return _gpio.analogRead(pin);
    }

    uint32_t Platform::nowMs() noexcept
    {
        return _time.nowMs();
    }

    uint64_t Platform::nowUs() noexcept
    {
        return _time.nowUs();
    }

    void Platform::delayMs(uint32_t ms) noexcept
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    void *Platform::alloc(size_t bytes, AllocCaps caps) noexcept
    {
        return _heap.alloc(bytes, caps);
    }

    void Platform::free(void *ptr) noexcept
    {
        _heap.free(ptr);
    }

    void *Platform::allocAligned(size_t bytes, size_t align, AllocCaps caps) noexcept
    {
        return _heap.allocAligned(bytes, align, caps);
    }

    void Platform::freeAligned(void *ptr) noexcept
    {
        _heap.freeAligned(ptr);
    }

#if PIPCORE_HAS_DISPLAY
    bool Platform::configDisplay(const DisplayConfig &cfg) noexcept
    {
        _lastError = PlatformError::None;
        _displayConfigured = false;
        _displayReady = false;
        if (cfg.width == 0 || cfg.height == 0)
        {
            _lastError = PlatformError::InvalidDisplayConfig;
            return false;
        }

        _display.reset();
        _transport.deinit();
        _transport.configure(cfg.mosi, cfg.sclk, cfg.cs, cfg.dc, cfg.rst, cfg.hz, kSpiMode);

        const bool ok = _display.configure(this, &_transport, cfg.width, cfg.height, cfg.order, cfg.invert, cfg.swap,
                                           cfg.xOffset, cfg.yOffset);
        if (!ok)
        {
            _lastError = PlatformError::DisplayConfigureFailed;
            return false;
        }

        _displayConfigured = true;
        return true;
    }

    bool Platform::beginDisplay(uint8_t rotation) noexcept
    {
        _displayReady = false;
        if (!_displayConfigured)
        {
            _lastError = PlatformError::InvalidDisplayConfig;
            return false;
        }

        if (!_display.begin(rotation))
        {
            _lastError = PlatformError::DisplayBeginFailed;
            return false;
        }

        _lastError = PlatformError::None;
        _displayReady = true;
        return true;
    }

    bool Platform::setDisplayRotation(uint8_t rotation) noexcept
    {
        if (!_displayConfigured || !_displayReady)
        {
            _lastError = PlatformError::InvalidDisplayConfig;
            return false;
        }

        if (!_display.setRotation(rotation))
        {
            _lastError = PlatformError::DisplayIoFailed;
            return false;
        }

        _lastError = PlatformError::None;
        return true;
    }

    pipcore::Display *Platform::display() noexcept
    {
        if (!_displayConfigured || !_displayReady)
            return nullptr;
        return &_display;
    }
#endif

    uint32_t Platform::freeHeapTotal() noexcept
    {
        return _heap.freeHeapTotal();
    }

    uint32_t Platform::freeHeapInternal() noexcept
    {
        return _heap.freeHeapInternal();
    }

    uint32_t Platform::largestFreeBlock() noexcept
    {
        return _heap.largestFreeBlock();
    }

    uint32_t Platform::minFreeHeap() noexcept
    {
        return _heap.minFreeHeap();
    }

    PlatformError Platform::lastError() const noexcept
    {
#if PIPCORE_HAS_DISPLAY
        if (_lastError == PlatformError::None && !_display.ioOk())
            return PlatformError::DisplayIoFailed;
#endif
        return _lastError;
    }

    const char *Platform::lastErrorText() const noexcept
    {
        if (_lastError != PlatformError::None)
            return platformErrorText(_lastError);

#if PIPCORE_HAS_DISPLAY
        if (!_display.ioOk())
            return _display.lastErrorText();
#endif
        return platformErrorText(PlatformError::None);
    }

}

namespace pipcore
{
    Platform *GetPlatform() noexcept
    {
        static esp32::Platform instance;
        return &instance;
    }
}

#if PIPCORE_ENABLE_DEBUG

void *operator new(size_t size)
{
    if (size == 0)
        size = 1;

    void *ptr = pipcore::debug::Tracker::instance().trackMalloc(size, "std.new", __builtin_return_address(0));
    if (!ptr)
    {
#if __cpp_exceptions
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    return ptr;
}

void *operator new[](size_t size)
{
    if (size == 0)
        size = 1;

    void *ptr = pipcore::debug::Tracker::instance().trackMalloc(size, "std.new[]", __builtin_return_address(0));
    if (!ptr)
    {
#if __cpp_exceptions
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
    return ptr;
}

void operator delete(void *ptr) noexcept
{
    pipcore::debug::Tracker::instance().trackFree(ptr);
}

void operator delete[](void *ptr) noexcept
{
    pipcore::debug::Tracker::instance().trackFree(ptr);
}

void operator delete(void *ptr, size_t) noexcept
{
    pipcore::debug::Tracker::instance().trackFree(ptr);
}

void operator delete[](void *ptr, size_t) noexcept
{
    pipcore::debug::Tracker::instance().trackFree(ptr);
}

#endif
