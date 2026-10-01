#pragma once

#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP

#include "Audio/Backend.hpp"

#include <atomic>
#include <cstdint>

#if PIPCORE_DESKTOP_AUDIO_MINIAUDIO
#include "miniaudio.h"
#else

struct ma_device
{
    void *pUserData = nullptr;
};
using ma_uint32 = uint32_t;
#endif

namespace pipcore::desktop
{
    class Audio final : public audio::Backend
    {
    public:
        Audio() = default;
        ~Audio() override;

        [[nodiscard]] bool init(const audio::BackendConfig &cfg) noexcept override;
        void deinit() noexcept override;

        [[nodiscard]] bool ready() const noexcept override { return _ready.load(std::memory_order_relaxed); }
        [[nodiscard]] uint32_t sampleRate() const noexcept override { return _sampleRate; }

        void writeInterleavedS16(const int16_t *, size_t) noexcept override {}

        void pumpCallback(int16_t *out, size_t frameCount) noexcept;

    private:
        static void dataCallback(ma_device *pDevice, void *pOut, const void *pInput, ma_uint32 frameCount) noexcept;

        std::atomic<bool> _ready{false};
        uint32_t _sampleRate = 44100;
        audio::BackendConfig::Pump _pump = nullptr;
        void *_pumpUser = nullptr;

#if PIPCORE_DESKTOP_AUDIO_MINIAUDIO
        ma_device _device = {};
        bool _deviceStarted = false;
#endif
    };
}

#endif
