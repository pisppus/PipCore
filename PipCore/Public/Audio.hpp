#pragma once

#include <cstdint>
#include <cstddef>
#include <mutex>

#include "Config.hpp"

#include "Audio/Backend.hpp"
#include "Audio/Pac.hpp"

namespace pipcore
{
    enum class Bus : uint8_t
    {
        MUSIC = 0,
        SFX = 1,
        UI = 2,
        AMBIENT = 3
    };

    struct SoundConfig
    {
        uint32_t sampleRate = 44100;
        int8_t bck = 13;
        int8_t ws = 15;
        int8_t dataOut = 17;
        uint8_t i2sPort = 0;
    };

    struct SoundHandle
    {
        uint16_t id = 0;
    };

    class Audio
    {
    public:
        static constexpr uint8_t MaxVoices = 16;

        struct BusQuota
        {
            uint8_t maxVoices[4] = {2, 10, 2, 2};
        };

        explicit Audio(audio::Backend &backend) noexcept;
        ~Audio();

        Audio(const Audio &) = delete;
        Audio &operator=(const Audio &) = delete;

        bool configure(const SoundConfig &cfg) noexcept;
        bool begin() noexcept;
        void end() noexcept;

        void setMasterVolume(float v01) noexcept;
        [[nodiscard]] float masterVolume() const noexcept;

        void setBusVolume(Bus bus, float v01) noexcept;
        [[nodiscard]] float busVolume(Bus bus) const noexcept;

        void setBusQuota(const BusQuota &q) noexcept;

        [[nodiscard]] SoundHandle play(const uint8_t *data, size_t dataSize, float volume = 1.0f,
                                       uint16_t gainL_q15 = 32767, uint16_t gainR_q15 = 32767, Bus bus = Bus::SFX,
                                       uint8_t priority = 5) noexcept;

        void stop(SoundHandle h) noexcept;
        void pause(SoundHandle h) noexcept;
        void resume(SoundHandle h) noexcept;
        void setVoiceGains(SoundHandle h, uint16_t gainL_q15, uint16_t gainR_q15) noexcept;

        [[nodiscard]] bool ready() const noexcept { return _ready; }

        [[nodiscard]] uint8_t activeVoices() const noexcept;
        [[nodiscard]] uint8_t countActiveOnBus(Bus bus) const noexcept;

        using MixHook = void (*)(int32_t *mixL, int32_t *mixR, size_t frames, void *user);
        void setMixHook(MixHook hook, void *user) noexcept;

    private:
        struct Voice
        {
            audio::PacVoiceState pac;

            uint16_t handleId = 0;
            uint16_t volume_q15 = 32767;
            uint16_t gainL_q15 = 32767;
            uint16_t gainR_q15 = 32767;
            uint16_t effGainL_q15 = 32767;
            uint16_t effGainR_q15 = 32767;

            uint8_t priority = 5;
            uint8_t bus = static_cast<uint8_t>(Bus::SFX);
            bool active = false;
            bool paused = false;
        };

        static constexpr size_t kMixBlockFrames = 256;

        audio::Backend &_backend;

        Voice _voices[MaxVoices] = {};
        uint16_t _nextHandleId = 1;
        uint16_t _masterVol_q15 = 26214;

        BusQuota _quota{};

        SoundConfig _cfg;
        bool _configured = false;
        bool _ready = false;

        MixHook _mixHook = nullptr;
        void *_mixHookUser = nullptr;

        int32_t _accumBufL[kMixBlockFrames] = {};
        int32_t _accumBufR[kMixBlockFrames] = {};

        uint16_t _busVol_q15[4] = {19660, 32767, 26214, 13107};

        mutable std::mutex _mutex;

        static void pumpTrampoline(int16_t *outInterleaved, size_t frames, void *user) noexcept;

        int findFreeVoiceSlot(uint8_t priority, Bus bus) noexcept;
        [[nodiscard]] uint8_t countActiveOnBusUnlocked(Bus bus) const noexcept;
        void recomputeVoiceGains(Voice &v) noexcept;
        void recomputeAllVoiceGains() noexcept;

        void pumpMixer(int16_t *outInterleaved, size_t frames) noexcept;
    };
}
