#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s_std.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "Config.hpp"
#include "Audio/Backend.hpp"

#if !PIPCORE_TARGET_ESP32
#error "pipcore::esp32::services::I2sOut requires ESP32 target"
#endif

namespace pipcore::esp32::services
{
    class I2sOut final : public audio::Backend
    {
    public:
        I2sOut() = default;
        ~I2sOut() override;

        [[nodiscard]] bool init(const audio::BackendConfig &cfg) noexcept override;
        void deinit() noexcept override;

        [[nodiscard]] bool ready() const noexcept override { return _ready.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t sampleRate() const noexcept override { return _sampleRate; }

        void writeInterleavedS16(const int16_t *interleavedLR, size_t frames) noexcept override;

    private:
        static void mixerTaskEntry(void *arg) noexcept;
        void mixerLoop() noexcept;

        i2s_chan_handle_t _txChan = nullptr;
        uint32_t _sampleRate = 44100;
        std::atomic<bool> _ready{false};

        std::atomic<bool> _run{false};

        std::atomic<bool> _taskExited{false};
        TaskHandle_t _taskHandle = nullptr;
        audio::BackendConfig::Pump _pump = nullptr;
        void *_pumpUser = nullptr;
        int16_t _blockBuf[256 * 2] = {};
    };
}
