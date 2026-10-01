#pragma once

#include <esp_timer.h>

#include <Platform.hpp>

namespace pipcore::esp32
{
    [[nodiscard]] inline uint32_t nowMs() noexcept
    {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
    }

    [[nodiscard]] bool nvsEnsureReady() noexcept;
}

namespace pipcore::esp32::services
{
    class Gpio
    {
    public:
        void pinModeInput(uint8_t pin, InputMode mode) const noexcept;
        [[nodiscard]] bool digitalRead(uint8_t pin) const noexcept;
        [[nodiscard]] int16_t analogRead(uint8_t pin) const noexcept;
    };

    class Heap
    {
    public:
        void *alloc(size_t bytes, AllocCaps caps) const noexcept;
        void free(void *ptr) const noexcept;
        void *allocAligned(size_t bytes, size_t align, AllocCaps caps) const noexcept;
        void freeAligned(void *ptr) const noexcept;
        [[nodiscard]] uint32_t freeHeapTotal() const noexcept;
        [[nodiscard]] uint32_t freeHeapInternal() const noexcept;
        [[nodiscard]] uint32_t largestFreeBlock() const noexcept;
        [[nodiscard]] uint32_t minFreeHeap() const noexcept;
    };

    class Time
    {
    public:
        [[nodiscard]] uint32_t nowMs() const noexcept;
        [[nodiscard]] uint64_t nowUs() const noexcept;
    };
}
