#pragma once

#include "Config.hpp"

#if !PIPCORE_TARGET_ESP32
#error "pipcore::esp32::Platform requires ESP32"
#endif

#include <Platform.hpp>
#include "Platforms/ESP32/Core/Core.hpp"
#if PIPCORE_ENABLE_PREFS
#include "Platforms/ESP32/Storage/Nvs.hpp"
#endif
#if PIPCORE_ENABLE_WIFI
#include "Platforms/ESP32/Network/Wifi.hpp"
#endif
#if PIPCORE_ENABLE_OTA
#include "Platforms/ESP32/Network/Ota.hpp"
#endif
#if PIPCORE_ENABLE_TOUCH
#include "Platforms/ESP32/Input/Touch.hpp"
#endif
#if PIPCORE_ENABLE_AUDIO
#include "Platforms/ESP32/Audio/I2s.hpp"
#include <Audio.hpp>
#endif
#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ST7789
#include "Display/ST/ST7789.hpp"
#elif PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ST7796
#include "Display/ST/ST7796.hpp"
#elif PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ILI9488
#include "Display/ILI9488/Display.hpp"
#elif PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) != PIPCORE_DISPLAY_TAG_NONE
#error "Unsupported PIPCORE_DISPLAY value for the ESP32 platform"
#endif
#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_NONE
#define PIPCORE_HAS_DISPLAY 0
#else
#define PIPCORE_HAS_DISPLAY 1
#include "Platforms/ESP32/Display/LcdTransport.hpp"
#endif

namespace pipcore::esp32
{
        class Platform final : public pipcore::Platform
        {
        public:
                Platform();
                ~Platform() override = default;

                void pinModeInput(uint8_t pin, InputMode mode) noexcept override;
                [[nodiscard]] bool digitalRead(uint8_t pin) noexcept override;
                [[nodiscard]] int16_t analogRead(uint8_t pin) noexcept override;

                [[nodiscard]] uint32_t nowMs() noexcept override;
                [[nodiscard]] uint64_t nowUs() noexcept override;
                void delayMs(uint32_t ms) noexcept override;

                void *alloc(size_t bytes, AllocCaps caps = AllocCaps::Default) noexcept override;
                void free(void *ptr) noexcept override;
                [[nodiscard]] void *allocAligned(size_t bytes, size_t align, AllocCaps caps = AllocCaps::Default) noexcept override;
                void freeAligned(void *ptr) noexcept override;

#if PIPCORE_HAS_DISPLAY
                [[nodiscard]] bool configDisplay(const DisplayConfig &cfg) noexcept override;
                [[nodiscard]] bool beginDisplay(uint8_t rotation) noexcept override;
                [[nodiscard]] bool setDisplayRotation(uint8_t rotation) noexcept override;
                [[nodiscard]] pipcore::Display *display() noexcept override;
#endif

                [[nodiscard]] uint32_t freeHeapTotal() noexcept override;
                [[nodiscard]] uint32_t freeHeapInternal() noexcept override;
                [[nodiscard]] uint32_t largestFreeBlock() noexcept override;
                [[nodiscard]] uint32_t minFreeHeap() noexcept override;
                [[nodiscard]] PlatformError lastError() const noexcept override;
                [[nodiscard]] const char *lastErrorText() const noexcept override;

#if PIPCORE_ENABLE_WIFI
                [[nodiscard]] pipcore::net::Backend *network() noexcept override { return &_wifi; }
                [[nodiscard]] const pipcore::net::Backend *network() const noexcept override { return &_wifi; }
#else
                [[nodiscard]] pipcore::net::Backend *network() noexcept override { return nullptr; }
                [[nodiscard]] const pipcore::net::Backend *network() const noexcept override { return nullptr; }
#endif

#if PIPCORE_ENABLE_OTA
                [[nodiscard]] pipcore::ota::Backend *update() noexcept override { return &_ota; }
                [[nodiscard]] const pipcore::ota::Backend *update() const noexcept override { return &_ota; }
#else
                [[nodiscard]] pipcore::ota::Backend *update() noexcept override { return nullptr; }
                [[nodiscard]] const pipcore::ota::Backend *update() const noexcept override { return nullptr; }
#endif

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

#if PIPCORE_ENABLE_PREFS
                [[nodiscard]] pipcore::prefs::Backend *prefs() noexcept override { return &_prefs; }
                [[nodiscard]] const pipcore::prefs::Backend *prefs() const noexcept override { return &_prefs; }
#else
                [[nodiscard]] pipcore::prefs::Backend *prefs() noexcept override { return nullptr; }
                [[nodiscard]] const pipcore::prefs::Backend *prefs() const noexcept override { return nullptr; }
#endif

        private:
                services::Time _time;
                services::Gpio _gpio;
                services::Heap _heap;
#if PIPCORE_ENABLE_PREFS
                services::Nvs _prefs;
#endif
#if PIPCORE_ENABLE_WIFI
                services::Wifi _wifi;
#endif
#if PIPCORE_ENABLE_OTA
                services::Ota _ota;
#endif
#if PIPCORE_ENABLE_TOUCH
                services::Touch _touch;
#endif
#if PIPCORE_ENABLE_AUDIO
                services::I2sOut _audioBackend;
                pipcore::Audio _audio;
#endif
#if PIPCORE_HAS_DISPLAY
                using SelectedDisplayTransport = LcdTransport;
                using SelectedDisplay =
#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ST7789
                    st7789::Display;
#elif PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ST7796
                    st7796::Display;
#elif PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ILI9488
                    ili9488::Display;
#endif
                static constexpr LcdTransport::Mode kSpiMode =
#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ILI9488
                    LcdTransport::Mode::DmaStream;
#else
                    LcdTransport::Mode::DriverCs;
#endif
                LcdTransport _transport;
                SelectedDisplay _display;
                bool _displayConfigured = false;
                bool _displayReady = false;
#endif
                PlatformError _lastError = PlatformError::None;
        };
}
