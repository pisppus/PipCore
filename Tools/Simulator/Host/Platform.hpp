#pragma once

#include "Config.hpp"
#if PIPCORE_TARGET_DESKTOP

#include <cstdio>
#include <cstring>

#include <Audio.hpp>
#include <Network/Ota.hpp>
#include <Network/Wifi.hpp>
#include <Platform.hpp>
#include <Prefs.hpp>
#include "Host/Audio/Audio.hpp"
#include "Host/Display/Simulator.hpp"
#if PIPCORE_ENABLE_TOUCH
#include "Host/Input/Touch.hpp"
#endif

namespace pipcore::desktop
{
    class Platform final : public pipcore::Platform
    {
    public:
        Platform()
#if PIPCORE_ENABLE_AUDIO
            : _audio(static_cast<pipcore::audio::Backend &>(_audioBackend))
#endif
        {
        }

        [[nodiscard]] uint32_t nowMs() noexcept override;
        [[nodiscard]] uint64_t nowUs() noexcept override;
        void delayMs(uint32_t ms) noexcept override;
        [[nodiscard]] bool shouldQuit() const noexcept override;

        void pinModeInput(uint8_t pin, InputMode mode) noexcept override;
        [[nodiscard]] bool digitalRead(uint8_t pin) noexcept override;
        [[nodiscard]] int16_t analogRead(uint8_t pin) noexcept override;

        [[nodiscard]] void *alloc(size_t bytes, AllocCaps caps = AllocCaps::Default) noexcept override;
        void free(void *ptr) noexcept override;
        [[nodiscard]] void *allocAligned(size_t bytes, size_t align, AllocCaps caps = AllocCaps::Default) noexcept override;
        void freeAligned(void *ptr) noexcept override;

        [[nodiscard]] bool configDisplay(const DisplayConfig &cfg) noexcept override;
        [[nodiscard]] bool beginDisplay(uint8_t rotation) noexcept override;
        [[nodiscard]] bool setDisplayRotation(uint8_t rotation) noexcept override;

        [[nodiscard]] pipcore::Display *display() noexcept override { return &_display; }

        [[nodiscard]] uint32_t freeHeapTotal() noexcept override { return 256U * 1024U * 1024U; }
        [[nodiscard]] uint32_t freeHeapInternal() noexcept override { return 256U * 1024U * 1024U; }
        [[nodiscard]] uint32_t largestFreeBlock() noexcept override { return 64U * 1024U * 1024U; }
        [[nodiscard]] uint32_t minFreeHeap() noexcept override { return 128U * 1024U * 1024U; }

        [[nodiscard]] net::Backend *network() noexcept override { return &_wifi; }
        [[nodiscard]] const net::Backend *network() const noexcept override { return &_wifi; }

        [[nodiscard]] ota::Backend *update() noexcept override { return &_ota; }
        [[nodiscard]] const ota::Backend *update() const noexcept override { return &_ota; }

#if PIPCORE_ENABLE_TOUCH
        [[nodiscard]] pipcore::Touch *touch() noexcept override { return &_touch; }
        [[nodiscard]] const pipcore::Touch *touch() const noexcept override { return &_touch; }
#else
        [[nodiscard]] pipcore::Touch *touch() noexcept override { return nullptr; }
        [[nodiscard]] const pipcore::Touch *touch() const noexcept override { return nullptr; }
#endif

#if PIPCORE_ENABLE_AUDIO
        [[nodiscard]] pipcore::Audio *audio() noexcept override { return &_audio; }
        [[nodiscard]] const pipcore::Audio *audio() const noexcept override { return &_audio; }
#else
        [[nodiscard]] pipcore::Audio *audio() noexcept override { return nullptr; }
        [[nodiscard]] const pipcore::Audio *audio() const noexcept override { return nullptr; }
#endif

        [[nodiscard]] pipcore::prefs::Backend *prefs() noexcept override { return &_prefs; }
        [[nodiscard]] const pipcore::prefs::Backend *prefs() const noexcept override { return &_prefs; }

    private:
        class WifiBackend final : public pipcore::net::Backend
        {
        public:
            void configure(const pipcore::net::WifiConfig &) noexcept override {}
            void request(bool enabled) noexcept override;
            void service() noexcept override {}

            [[nodiscard]] pipcore::net::WifiState state() const noexcept override { return _state; }

        private:
            pipcore::net::WifiState _state = pipcore::net::WifiState::Off;
        };

        class PrefsBackend final : public pipcore::prefs::Backend
        {
        public:
            static constexpr int kEntries = 16;
            static constexpr size_t kValueCap = 56;

            struct Entry
            {
                bool used = false;
                char key[24] = {};
                uint8_t data[kValueCap] = {};
                uint16_t len = 0;
            };

            [[nodiscard]] bool getU8(const char *key, uint8_t &out) noexcept override
            {
                uint64_t v = 0;
                if (!getNum(key, v))
                    return false;
                out = static_cast<uint8_t>(v);
                return true;
            }
            [[nodiscard]] bool getU16(const char *key, uint16_t &out) noexcept override
            {
                uint64_t v = 0;
                if (!getNum(key, v))
                    return false;
                out = static_cast<uint16_t>(v);
                return true;
            }
            [[nodiscard]] bool getU32(const char *key, uint32_t &out) noexcept override
            {
                uint64_t v = 0;
                if (!getNum(key, v))
                    return false;
                out = static_cast<uint32_t>(v);
                return true;
            }
            [[nodiscard]] bool getU64(const char *key, uint64_t &out) noexcept override
            {
                return getNum(key, out);
            }
            [[nodiscard]] bool getI32(const char *key, int32_t &out) noexcept override
            {
                uint64_t v = 0;
                if (!getNum(key, v))
                    return false;
                out = static_cast<int32_t>(static_cast<uint32_t>(v));
                return true;
            }
            [[nodiscard]] bool getStr(const char *key, char *out, size_t cap) noexcept override
            {
                Entry *e = find(key);
                if (!e || e->len + 1 > cap)
                    return false;
                std::memcpy(out, e->data, e->len);
                out[e->len] = '\0';
                return true;
            }
            [[nodiscard]] bool getBlob(const char *key, void *out, size_t &len) noexcept override
            {
                Entry *e = find(key);
                if (!e || e->len > len)
                    return false;
                std::memcpy(out, e->data, e->len);
                len = e->len;
                return true;
            }

            bool setU8(const char *key, uint8_t v) noexcept override { return setNum(key, v); }
            bool setU16(const char *key, uint16_t v) noexcept override { return setNum(key, v); }
            bool setU32(const char *key, uint32_t v) noexcept override { return setNum(key, v); }
            bool setU64(const char *key, uint64_t v) noexcept override { return setNum(key, v); }
            bool setI32(const char *key, int32_t v) noexcept override { return setNum(key, static_cast<uint32_t>(v)); }
            bool setStr(const char *key, const char *v) noexcept override
            {
                const size_t len = std::strlen(v);
                return len <= kValueCap && setBytes(key, v, len);
            }
            bool setBlob(const char *key, const void *data, size_t len) noexcept override
            {
                return len <= kValueCap && setBytes(key, data, len);
            }

            bool eraseKey(const char *key) noexcept override
            {
                for (Entry &e : _entries)
                {
                    if (e.used && std::strcmp(e.key, key) == 0)
                    {
                        e.used = false;
                        return true;
                    }
                }
                return false;
            }
            bool eraseAll() noexcept override
            {
                for (Entry &e : _entries)
                    e.used = false;
                return true;
            }

        private:
            [[nodiscard]] Entry *find(const char *key) noexcept
            {
                for (Entry &e : _entries)
                    if (e.used && std::strcmp(e.key, key) == 0)
                        return &e;
                return nullptr;
            }
            [[nodiscard]] bool getNum(const char *key, uint64_t &out) noexcept
            {
                Entry *e = find(key);
                if (!e || e->len != sizeof(out))
                    return false;
                std::memcpy(&out, e->data, sizeof(out));
                return true;
            }
            [[nodiscard]] bool setNum(const char *key, uint64_t v) noexcept
            {
                return setBytes(key, &v, sizeof(v));
            }
            [[nodiscard]] bool setBytes(const char *key, const void *data, size_t len) noexcept
            {
                if (len > kValueCap)
                    return false;
                Entry *e = find(key);
                if (!e)
                {
                    for (Entry &free2 : _entries)
                    {
                        if (!free2.used)
                        {
                            e = &free2;
                            break;
                        }
                    }
                    if (!e)
                        return false;
                    std::snprintf(e->key, sizeof(e->key), "%s", key);
                    e->used = true;
                }
                std::memcpy(e->data, data, len);
                e->len = static_cast<uint16_t>(len);
                return true;
            }

            Entry _entries[kEntries] = {};
        };

        class OtaBackend final : public pipcore::ota::Backend
        {
        public:
            void markAppValid() noexcept override;
            void configure(const pipcore::ota::Options &opt, pipcore::ota::StatusCallback cb, void *user) noexcept override;
            void requestCheck(pipcore::ota::CheckMode mode) noexcept override;
            void requestInstall() noexcept override;
            void requestStableList() noexcept override;
            [[nodiscard]] bool stableListReady() const noexcept override { return false; }
            [[nodiscard]] uint8_t stableListCount() const noexcept override { return 0; }
            [[nodiscard]] const char *stableListVersion(uint8_t idx) const noexcept override;
            void requestInstallStableVersion(const char *version) noexcept override;
            void cancel() noexcept override;
            void service() noexcept override {}
            [[nodiscard]] const pipcore::ota::Status &status() const noexcept override { return _status; }

        private:
            void fail(pipcore::ota::Error error) noexcept;
            void notify() noexcept;

            pipcore::ota::Options _options = {};
            pipcore::ota::Status _status = {};
            pipcore::ota::StatusCallback _callback = nullptr;
            void *_callbackUser = nullptr;
        };

        pipcore::simulator::Display _display;
        DisplayConfig _config = {};
        bool _configValid = false;
        WifiBackend _wifi = {};
        OtaBackend _ota = {};
        PrefsBackend _prefs = {};
#if PIPCORE_ENABLE_TOUCH
        Touch _touch;
#endif
#if PIPCORE_ENABLE_AUDIO

        desktop::Audio _audioBackend;
        pipcore::Audio _audio;
#endif
    };
}

#endif
