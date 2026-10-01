#pragma once

#include <cstddef>
#include <cstdint>

#include "Display/Commands.hpp"

namespace pipcore::ili9488
{
    using IoError = pipcore::display::IoError;
    using Transport = pipcore::display::Transport;

    class Driver : public pipcore::display::CmdWriter
    {
    public:
        Driver() = default;

        [[nodiscard]] bool configure(Transport *transport, uint16_t width, uint16_t height, uint8_t order = 1,
                                     bool invert = true, bool swap = false, int16_t xOffset = 0, int16_t yOffset = 0);

        [[nodiscard]] bool begin(uint8_t rotation) noexcept;
        [[nodiscard]] bool setRotation(uint8_t rotation) noexcept;
        void reset() noexcept;

        [[nodiscard]] uint16_t width() const noexcept { return _width; }
        [[nodiscard]] uint16_t height() const noexcept { return _height; }
        [[nodiscard]] size_t preferredChunkBytes() const noexcept
        {
            return _transport ? _transport->preferredChunkBytes() : 0U;
        }

        [[nodiscard]] bool setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) noexcept;
        [[nodiscard]] bool beginWriteWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) noexcept;
        void endWrite() noexcept;
        [[nodiscard]] bool writePixels666(const uint8_t *bytes, size_t len) noexcept;
        [[nodiscard]] bool supportsDirectPixels() const noexcept
        {
            return _transport && _transport->supportsDirectPixels();
        }
        [[nodiscard]] uint8_t *directPixelsBuffer(size_t &capacity)
        {
            return _transport ? _transport->directPixelsBuffer(capacity) : nullptr;
        }
        [[nodiscard]] bool submitDirectPixels(size_t len) { return _transport && _transport->submitDirectPixels(len); }
        [[nodiscard]] bool fillScreen565(uint16_t color565) noexcept;

    private:
        [[nodiscard]] bool hardReset() noexcept;
        [[nodiscard]] bool setRotationInternal(uint8_t rotation) noexcept;
        [[nodiscard]] bool flushTransport() noexcept;

        uint16_t _width = 0;
        uint16_t _height = 0;
        uint16_t _physWidth = 0;
        uint16_t _physHeight = 0;
        uint8_t _rotation = 0;
        int16_t _xStart = 0;
        int16_t _yStart = 0;
        int16_t _xOffsetCfg = 0;
        int16_t _yOffsetCfg = 0;
        uint8_t _order = 1;
        bool _invert = true;
        bool _swap = false;
        bool _initialized = false;
    };
}
