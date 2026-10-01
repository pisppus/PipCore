#include <algorithm>
#include <cstring>

#include "Config.hpp"
#include <Audio.hpp>

namespace pipcore
{
    Audio::Audio(audio::Backend &backend) noexcept : _backend(backend) {}

    Audio::~Audio()
    {
        end();
    }

    bool Audio::configure(const SoundConfig &cfg) noexcept
    {
        end();
        _cfg = cfg;
        _configured = true;
        return true;
    }

    bool Audio::begin() noexcept
    {
        if (_ready)
            return true;
        if (!_configured)
            (void)configure(SoundConfig());

        audio::BackendConfig bcfg;
        bcfg.sampleRate = _cfg.sampleRate;
        bcfg.i2sPort = _cfg.i2sPort;
        bcfg.bck = _cfg.bck;
        bcfg.ws = _cfg.ws;
        bcfg.dataOut = _cfg.dataOut;
        bcfg.pump = &Audio::pumpTrampoline;
        bcfg.pumpUser = this;
        if (!_backend.init(bcfg))
        {
            return false;
        }

        _ready = true;
        return true;
    }

    void Audio::end() noexcept
    {
        _ready = false;
        _backend.deinit();

        const std::scoped_lock lock(_mutex);
        for (uint8_t i = 0; i < MaxVoices; ++i)
            _voices[i] = Voice();
    }

    void Audio::setMasterVolume(float v01) noexcept
    {
        const std::scoped_lock lock(_mutex);
        v01 = std::clamp(v01, 0.0f, 1.0f);
        _masterVol_q15 = static_cast<uint16_t>(v01 * 32767.0f);
    }

    float Audio::masterVolume() const noexcept
    {
        const std::scoped_lock lock(_mutex);
        return static_cast<float>(_masterVol_q15) / 32767.0f;
    }

    void Audio::setBusVolume(Bus bus, float v01) noexcept
    {
        const std::scoped_lock lock(_mutex);
        v01 = std::clamp(v01, 0.0f, 1.0f);
        _busVol_q15[static_cast<uint8_t>(bus)] = static_cast<uint16_t>(v01 * 32767.0f);
        recomputeAllVoiceGains();
    }

    float Audio::busVolume(Bus bus) const noexcept
    {
        const std::scoped_lock lock(_mutex);
        return static_cast<float>(_busVol_q15[static_cast<uint8_t>(bus)]) / 32767.0f;
    }

    void Audio::setBusQuota(const BusQuota &q) noexcept
    {
        const std::scoped_lock lock(_mutex);
        _quota = q;
    }

    void Audio::setMixHook(MixHook hook, void *user) noexcept
    {
        const std::scoped_lock lock(_mutex);
        _mixHook = hook;
        _mixHookUser = user;
    }

    uint8_t Audio::countActiveOnBusUnlocked(Bus bus) const noexcept
    {
        uint8_t n = 0;
        const uint8_t b = static_cast<uint8_t>(bus);
        for (int i = 0; i < MaxVoices; ++i)
            if (_voices[i].active && _voices[i].bus == b)
                ++n;
        return n;
    }

    uint8_t Audio::countActiveOnBus(Bus bus) const noexcept
    {
        const std::scoped_lock lock(_mutex);
        return countActiveOnBusUnlocked(bus);
    }

    uint8_t Audio::activeVoices() const noexcept
    {
        const std::scoped_lock lock(_mutex);
        uint8_t n = 0;
        for (int i = 0; i < MaxVoices; ++i)
            if (_voices[i].active && !_voices[i].paused)
                ++n;
        return n;
    }

    int Audio::findFreeVoiceSlot(uint8_t priority, Bus bus) noexcept
    {
        const uint8_t b = static_cast<uint8_t>(bus);

        if (countActiveOnBusUnlocked(bus) < _quota.maxVoices[b])
        {
            for (int i = 0; i < MaxVoices; ++i)
                if (!_voices[i].active)
                    return i;
        }

        int stealIdx = -1;
        uint8_t lowestPrio = priority;
        for (int i = 0; i < MaxVoices; ++i)
        {
            if (!_voices[i].active)
                continue;
            if (_voices[i].bus != b)
                continue;
            if (_voices[i].priority < lowestPrio)
            {
                lowestPrio = _voices[i].priority;
                stealIdx = i;
            }
        }
        return stealIdx;
    }

    SoundHandle Audio::play(const uint8_t *data, size_t dataSize, float volume, uint16_t gainL_q15, uint16_t gainR_q15,
                            Bus bus, uint8_t priority) noexcept
    {
        if (!_ready || !data || dataSize < sizeof(audio::PACHeader))
            return {0};

        if ((reinterpret_cast<uintptr_t>(data) & 3u) != 0u)
            return {0};

        const auto *hdr = reinterpret_cast<const audio::PACHeader *>(data);
        if (!hdr->isValid())
            return {0};

        if (hdr->dataOffset < sizeof(audio::PACHeader) || hdr->dataOffset > dataSize ||
            hdr->dataSize > dataSize - hdr->dataOffset)
            return {0};
        if (hdr->frameCount == 0)
            return {0};
        if (hdr->sourceRate < 1000 || hdr->sourceRate > 192000)
            return {0};

        const std::scoped_lock lock(_mutex);

        int slot = findFreeVoiceSlot(priority, bus);
        if (slot < 0)
            return {0};

        Voice &v = _voices[slot];
        v = Voice();

        v.pac.data = data + hdr->dataOffset;
        v.pac.dataEnd = v.pac.data + hdr->dataSize;
        v.pac.frameCount = hdr->frameCount;
        v.pac.loopStart = hdr->loopStart;
        v.pac.loopEnd = hdr->loopEnd;
        v.pac.isLoop = hdr->isLoop();

        if (v.pac.loopEnd > v.pac.frameCount)
            v.pac.loopEnd = v.pac.frameCount;
        if (v.pac.loopStart >= v.pac.loopEnd || v.pac.loopStart >= v.pac.frameCount)
        {
            v.pac.isLoop = false;
            v.pac.loopStart = 0;
            v.pac.loopEnd = v.pac.frameCount;
        }

        const uint32_t outRate = _backend.sampleRate();
        const uint32_t rate = (outRate != 0) ? outRate : (hdr->nativeRate ? hdr->nativeRate : _cfg.sampleRate);
        if (rate > 0)
            v.pac.srcStep = (static_cast<uint32_t>(hdr->sourceRate) << 16) / rate;
        else
            v.pac.srcStep = 65536;
        v.pac.isPassthrough = (v.pac.srcStep == 65536u);
        v.pac.blockPtr = v.pac.data;
        v.pac.active = true;

        v.handleId = _nextHandleId++;
        if (_nextHandleId == 0)
            _nextHandleId = 1;

        v.gainL_q15 = gainL_q15;
        v.gainR_q15 = gainR_q15;
        v.volume_q15 = static_cast<uint16_t>(std::clamp(volume, 0.0f, 1.0f) * 32767.0f);
        v.priority = priority;
        v.bus = static_cast<uint8_t>(bus);

        recomputeVoiceGains(v);
        v.active = true;
        v.paused = false;

        return SoundHandle{v.handleId};
    }

    void Audio::stop(SoundHandle h) noexcept
    {
        if (h.id == 0 || !_ready)
            return;
        const std::scoped_lock lock(_mutex);
        for (int i = 0; i < MaxVoices; ++i)
            if (_voices[i].handleId == h.id)
            {
                _voices[i].active = false;
                break;
            }
    }

    void Audio::pause(SoundHandle h) noexcept
    {
        if (h.id == 0 || !_ready)
            return;
        const std::scoped_lock lock(_mutex);
        for (int i = 0; i < MaxVoices; ++i)
            if (_voices[i].handleId == h.id)
            {
                _voices[i].paused = true;
                break;
            }
    }

    void Audio::resume(SoundHandle h) noexcept
    {
        if (h.id == 0 || !_ready)
            return;
        const std::scoped_lock lock(_mutex);
        for (int i = 0; i < MaxVoices; ++i)
            if (_voices[i].handleId == h.id)
            {
                _voices[i].paused = false;
                break;
            }
    }

    void Audio::setVoiceGains(SoundHandle h, uint16_t gainL_q15, uint16_t gainR_q15) noexcept
    {
        if (h.id == 0 || !_ready)
            return;
        const std::scoped_lock lock(_mutex);
        for (int i = 0; i < MaxVoices; ++i)
            if (_voices[i].handleId == h.id && _voices[i].active)
            {
                _voices[i].gainL_q15 = gainL_q15;
                _voices[i].gainR_q15 = gainR_q15;
                recomputeVoiceGains(_voices[i]);
                break;
            }
    }

    void Audio::recomputeVoiceGains(Voice &v) noexcept
    {
        int32_t busVol = static_cast<int32_t>(_busVol_q15[v.bus]);
        int32_t volL = (static_cast<int32_t>(v.volume_q15) * static_cast<int32_t>(v.gainL_q15)) >> 15;
        int32_t volR = (static_cast<int32_t>(v.volume_q15) * static_cast<int32_t>(v.gainR_q15)) >> 15;
        v.effGainL_q15 = static_cast<uint16_t>((volL * busVol) >> 15);
        v.effGainR_q15 = static_cast<uint16_t>((volR * busVol) >> 15);
    }

    void Audio::recomputeAllVoiceGains() noexcept
    {
        for (int i = 0; i < MaxVoices; ++i)
            if (_voices[i].active)
                recomputeVoiceGains(_voices[i]);
    }

    void Audio::pumpTrampoline(int16_t *outInterleaved, size_t frames, void *user) noexcept
    {
        static_cast<Audio *>(user)->pumpMixer(outInterleaved, frames);
    }

    void Audio::pumpMixer(int16_t *outInterleaved, size_t frames) noexcept
    {
        MixHook hook = nullptr;
        void *hookUser = nullptr;
        int32_t masterVol = 0;
        const size_t accumFrames = (frames < kMixBlockFrames) ? frames : kMixBlockFrames;

        {
            const std::scoped_lock lock(_mutex);

            masterVol = static_cast<int32_t>(_masterVol_q15);

            std::memset(_accumBufL, 0, accumFrames * sizeof(int32_t));
            std::memset(_accumBufR, 0, accumFrames * sizeof(int32_t));

            for (int i = 0; i < MaxVoices; ++i)
            {
                Voice &v = _voices[i];
                if (!v.active || v.paused)
                    continue;

                int32_t volL = static_cast<int32_t>(v.effGainL_q15);
                int32_t volR = static_cast<int32_t>(v.effGainR_q15);

                int32_t *accL = _accumBufL;
                int32_t *accR = _accumBufR;

                for (size_t f = 0; f < accumFrames; ++f)
                {
                    int16_t sL = 0, sR = 0;
                    v.pac.decodeMixFrame(sL, sR);
                    if (!v.pac.active)
                    {
                        v.active = false;
                        break;
                    }
                    *accL++ += (static_cast<int32_t>(sL) * volL) >> 15;
                    *accR++ += (static_cast<int32_t>(sR) * volR) >> 15;
                }
            }
            hook = _mixHook;
            hookUser = _mixHookUser;
        }

        if (hook)
            hook(_accumBufL, _accumBufR, accumFrames, hookUser);

        for (size_t f = 0; f < accumFrames; ++f)
        {
            outInterleaved[f * 2 + 0] = audio::clampSat16((_accumBufL[f] * masterVol) >> 15);
            outInterleaved[f * 2 + 1] = audio::clampSat16((_accumBufR[f] * masterVol) >> 15);
        }

        for (size_t f = accumFrames; f < frames; ++f)
        {
            outInterleaved[f * 2 + 0] = 0;
            outInterleaved[f * 2 + 1] = 0;
        }
    }
}
