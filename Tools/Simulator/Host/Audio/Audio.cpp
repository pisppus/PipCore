#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP
#if PIPCORE_DESKTOP_AUDIO_MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#endif
#include <cstdio>
#include <cstring>

#include "Host/Audio/Audio.hpp"
#include <Audio.hpp>
#include <Log.hpp>

namespace pipcore::desktop
{
    Audio::~Audio()
    {
        deinit();
    }

    bool Audio::init(const audio::BackendConfig &cfg) noexcept
    {
        deinit();
        _sampleRate = cfg.sampleRate;
        _pump = cfg.pump;
        _pumpUser = cfg.pumpUser;

        log::info("[audio] desktop backend @ %u Hz", static_cast<unsigned>(_sampleRate));
#if PIPCORE_DESKTOP_AUDIO_MINIAUDIO
        ma_device_config dc = ma_device_config_init(ma_device_type_playback);
        dc.playback.format = ma_format_s16;
        dc.playback.channels = 2;
        dc.sampleRate = _sampleRate;
        dc.dataCallback = &Audio::dataCallback;
        dc.pUserData = this;

        if (ma_device_init(NULL, &dc, &_device) != MA_SUCCESS)
        {
            log::error("[audio] miniaudio: ma_device_init failed");
            return false;
        }
        if (ma_device_start(&_device) != MA_SUCCESS)
        {
            log::error("[audio] miniaudio: ma_device_start failed");
            ma_device_uninit(&_device);
            return false;
        }
        _deviceStarted = true;
        log::info("[audio] miniaudio backend ready");
#else
        log::info("[audio] miniaudio disabled - running headless");
#endif

        _ready.store(true, std::memory_order_relaxed);
        return true;
    }

    void Audio::deinit() noexcept
    {
        _ready.store(false, std::memory_order_relaxed);
#if PIPCORE_DESKTOP_AUDIO_MINIAUDIO
        if (_deviceStarted)
        {
            ma_device_uninit(&_device);
            _deviceStarted = false;
        }
#endif
    }

    void Audio::pumpCallback(int16_t *out, size_t frameCount) noexcept
    {
        if (!out || frameCount == 0)
            return;
        if (!_pump)
        {
            std::memset(out, 0, frameCount * 2 * sizeof(int16_t));
            return;
        }

        _pump(out, frameCount, _pumpUser);
    }

    void Audio::dataCallback(ma_device *pDevice, void *pOut, const void *, ma_uint32 frameCount) noexcept
    {
        if (!pDevice)
        {
            if (pOut)
                std::memset(pOut, 0, frameCount * sizeof(int16_t) * 2);
            return;
        }
        auto *self = static_cast<Audio *>(pDevice->pUserData);
        if (!self)
        {
            if (pOut)
                std::memset(pOut, 0, frameCount * sizeof(int16_t) * 2);
            return;
        }
        self->pumpCallback(static_cast<int16_t *>(pOut), frameCount);
    }
}

#endif
