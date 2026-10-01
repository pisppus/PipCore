#pragma once

#include <cstdint>
#include <cstddef>

#include "Config.hpp"
#include "Core/Pixel.hpp"
#include "Display/Commands.hpp"

namespace pipcore::detail
{
    enum class StDisplayType : uint8_t
    {
        ST7789,
        ST7796
    };

    template <StDisplayType Type>
    class StDriver : public pipcore::display::CmdWriter
    {
    public:
        using Transport = pipcore::display::Transport;
        using IoError = pipcore::display::IoError;

        StDriver() = default;

        [[nodiscard]] bool configure(Transport *transport, uint16_t width, uint16_t height, uint8_t order = 0,
                                     bool invert = true, bool swap = false, int16_t xOffset = 0,
                                     int16_t yOffset = 0) noexcept
        {
            if (!transport || !width || !height || xOffset < 0 || yOffset < 0) [[unlikely]]
            {
                reset();
                _lastError = IoError::InvalidConfig;
                return false;
            }

            _transport = transport;
            _width = _physWidth = width;
            _height = _physHeight = height;
            _xStart = _xOffsetCfg = xOffset;
            _yStart = _yOffsetCfg = yOffset;

            _order = static_cast<uint8_t>(order == 1);
            _invert = invert;
            _swap = swap;
            _initialized = false;
            _lastError = IoError::None;
            _transport->clearError();
            return true;
        }

        void reset() noexcept { *this = StDriver(); }

        [[nodiscard]] bool begin(uint8_t rotation) noexcept
        {
            _initialized = false;
            _lastError = IoError::None;

            if (!_transport || !_width || !_height) [[unlikely]]
            {
                _lastError = IoError::InvalidConfig;
                return false;
            }

            _transport->clearError();
            if (!_transport->init()) [[unlikely]]
                return failFromTransport(IoError::NotReady);

            if (!hardReset()) [[unlikely]]
                return false;

            if (!sendCommand(pipcore::display::CmdSWRESET)) [[unlikely]]
                return false;
            _transport->delayMs(120);

            if (!sendCommand(pipcore::display::CmdSLPOUT)) [[unlikely]]
                return false;

            if constexpr (Type == StDisplayType::ST7796)
            {
                _transport->delayMs(120);
            }
            else
            {
                _transport->delayMs(10);
            }

            uint8_t colmod = pipcore::display::Colmod16bpp;
            if (!writeReg(pipcore::display::CmdCOLMOD, &colmod, 1)) [[unlikely]]
                return false;

            if constexpr (Type == StDisplayType::ST7789)
            {

                // FRCTRL2: frame rate 60 Hz
                const uint8_t b6_data[] = {0x0A, 0x82};
                // PORCTRL: back/front porch 12 lines
                const uint8_t b2_data[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
                // GCTRL: gate rails VGH 13.26 V, VGL -10.43 V
                const uint8_t b7_data[] = {0x35};
                // VCOMS: COM voltage, panel-tuned
                const uint8_t bb_data[] = {0x28};
                // LCMCTRL: panel drive flags (vendor)
                const uint8_t c0_data[] = {0x0C};
                // VDVVRHEN: unlock VRH/VDV writes
                const uint8_t c2_data[] = {0x01, 0xFF};
                // VRHS: GVDD reference
                const uint8_t c3_data[] = {0x10};
                // VDVS: VDV offset 0 V
                const uint8_t c4_data[] = {0x20};
                // FRCTRL1: idle frame rate 60 Hz
                const uint8_t c6_data[] = {0x0F};
                // PWCTRL1: AVDD 6.8 V, AVCL -4.8 V, VDS 2.3 V
                const uint8_t d0_data[] = {0xA4, 0xA1};
                // PVGAMCTRL: positive gamma curve
                const uint8_t e0_data[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x32,
                                           0x44, 0x42, 0x06, 0x0E, 0x12, 0x14, 0x17};
                // NVGAMCTRL: negative gamma curve
                const uint8_t e1_data[] = {0xD0, 0x00, 0x02, 0x07, 0x0A, 0x28, 0x31,
                                           0x54, 0x47, 0x0E, 0x1C, 0x17, 0x1B, 0x1E};

                if (!writeReg(0xB6, b6_data, sizeof(b6_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xB2, b2_data, sizeof(b2_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xB7, b7_data, sizeof(b7_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xBB, bb_data, sizeof(bb_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC0, c0_data, sizeof(c0_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC2, c2_data, sizeof(c2_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC3, c3_data, sizeof(c3_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC4, c4_data, sizeof(c4_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC6, c6_data, sizeof(c6_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xD0, d0_data, sizeof(d0_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xE0, e0_data, sizeof(e0_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xE1, e1_data, sizeof(e1_data))) [[unlikely]]
                    return false;

                if (!sendCommand(_invert ? pipcore::display::CmdINVON : pipcore::display::CmdINVOFF)) [[unlikely]]
                    return false;

                if (!sendCommand(pipcore::display::CmdNORON)) [[unlikely]]
                    return false;

                if (!setRotationInternal(rotation)) [[unlikely]]
                    return false;
            }
            else
            {
                // CMD2 unlock: enable vendor command page
                const uint8_t f0_unlock1[] = {0xC3};
                const uint8_t f0_unlock2[] = {0x96};
                // INVCTR: dot inversion in normal mode
                const uint8_t b4_data[] = {0x01};
                // FRCTRL2: frame-rate timing (vendor)
                const uint8_t b6_data[] = {0x80, 0x02, 0x3B};
                // Gate/source drive tuning (vendor)
                const uint8_t e8_data[] = {0x40, 0x8A, 0x00, 0x00, 0x29, 0x19, 0xA5, 0x33};
                // CMD2 power tuning (vendor)
                const uint8_t c1_data[] = {0x06};
                const uint8_t c2_data[] = {0xA7};
                const uint8_t c5_data[] = {0x18};
                // PVGAMCTRL: positive gamma curve
                const uint8_t e0_data[] = {0xF0, 0x09, 0x0B, 0x06, 0x04, 0x15, 0x2F,
                                           0x54, 0x42, 0x3C, 0x17, 0x14, 0x18, 0x1B};
                // NVGAMCTRL: negative gamma curve
                const uint8_t e1_data[] = {0xE0, 0x09, 0x0B, 0x06, 0x04, 0x03, 0x2B,
                                           0x43, 0x42, 0x3B, 0x16, 0x14, 0x17, 0x1B};
                // CMD2 re-lock
                const uint8_t f0_lock1[] = {0x3C};
                const uint8_t f0_lock2[] = {0x69};

                if (!writeReg(0xF0, f0_unlock1, sizeof(f0_unlock1))) [[unlikely]]
                    return false;
                if (!writeReg(0xF0, f0_unlock2, sizeof(f0_unlock2))) [[unlikely]]
                    return false;
                if (!writeReg(0xB4, b4_data, sizeof(b4_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xB6, b6_data, sizeof(b6_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xE8, e8_data, sizeof(e8_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC1, c1_data, sizeof(c1_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC2, c2_data, sizeof(c2_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xC5, c5_data, sizeof(c5_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xE0, e0_data, sizeof(e0_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xE1, e1_data, sizeof(e1_data))) [[unlikely]]
                    return false;
                if (!writeReg(0xF0, f0_lock1, sizeof(f0_lock1))) [[unlikely]]
                    return false;
                if (!writeReg(0xF0, f0_lock2, sizeof(f0_lock2))) [[unlikely]]
                    return false;

                if (!setRotationInternal(rotation)) [[unlikely]]
                    return false;

                if (!sendCommand(_invert ? pipcore::display::CmdINVON : pipcore::display::CmdINVOFF)) [[unlikely]]
                    return false;

                if (!sendCommand(pipcore::display::CmdNORON)) [[unlikely]]
                    return false;
            }

            if (!sendCommand(pipcore::display::CmdDISPON)) [[unlikely]]
                return false;
            _transport->delayMs(20);

            _initialized = true;
            _lastError = IoError::None;
            return true;
        }

        [[nodiscard]] bool setRotation(uint8_t rotation) noexcept
        {
            if (!_transport || !_initialized) [[unlikely]]
            {
                _lastError = IoError::NotReady;
                return false;
            }
            if (!_transport->waitComplete()) [[unlikely]]
                return false;

            return setRotationInternal(rotation);
        }

        [[nodiscard]] uint16_t width() const noexcept { return _width; }
        [[nodiscard]] uint16_t height() const noexcept { return _height; }
        [[nodiscard]] bool swapBytes() const noexcept { return _swap; }

        [[nodiscard]] inline bool PIPCORE_ALWAYS_INLINE waitComplete() noexcept
        {
            if (!_transport)
                return false;
            return _transport->waitComplete();
        }

        [[nodiscard]] inline bool PIPCORE_ALWAYS_INLINE waitOldest() noexcept
        {
            if (!_transport)
                return false;
            return _transport->waitOldest();
        }

        [[nodiscard]] inline bool PIPCORE_ALWAYS_INLINE setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1,
                                                                      uint16_t y1) noexcept
        {
            if (!_transport || !_initialized || !_width || !_height || x1 < x0 || y1 < y0)
            {
                _lastError = (_transport && _initialized) ? IoError::InvalidConfig : IoError::NotReady;
                return false;
            }
            if (x0 >= _width || y0 >= _height)
            {
                _lastError = IoError::InvalidConfig;
                return false;
            }

            if (x1 >= _width)
                x1 = static_cast<uint16_t>(_width - 1U);
            if (y1 >= _height)
                y1 = static_cast<uint16_t>(_height - 1U);

            const uint16_t xs = static_cast<uint16_t>(x0 + _xStart);
            const uint16_t xe = static_cast<uint16_t>(x1 + _xStart);
            const uint16_t ys = static_cast<uint16_t>(y0 + _yStart);
            const uint16_t ye = static_cast<uint16_t>(y1 + _yStart);

            if (!_transport->writeAddrWindow(xs, xe, ys, ye))
            {
                return failFromTransport(IoError::CmdTx);
            }

            return true;
        }

        [[nodiscard]] inline bool PIPCORE_ALWAYS_INLINE writePixels565(const uint16_t *pixels, size_t pixelCount) noexcept
        {
            if (!_transport || !_initialized || !pixels || !pixelCount)
            {
                _lastError = (_transport && _initialized) ? IoError::InvalidConfig : IoError::NotReady;
                return false;
            }
            return sendPixels(pixels, pixelCount * sizeof(uint16_t));
        }

        [[nodiscard]] inline bool PIPCORE_ALWAYS_INLINE writePixels565Async(const uint16_t *pixels,
                                                                            size_t pixelCount) noexcept
        {
            if (!_transport || !_initialized || !pixels || !pixelCount)
            {
                _lastError = (_transport && _initialized) ? IoError::InvalidConfig : IoError::NotReady;
                return false;
            }
            return _transport->writePixelsAsync(pixels, pixelCount * sizeof(uint16_t));
        }

        [[nodiscard]] PIPCORE_HOT bool fillScreen565(uint16_t color565, bool swapBytes = false) noexcept
        {
            if (!_transport || !_initialized || !_width || !_height) [[unlikely]]
            {
                _lastError = (_transport && _initialized) ? IoError::InvalidConfig : IoError::NotReady;
                return false;
            }

            if (!setAddrWindow(0, 0, static_cast<uint16_t>(_width - 1U), static_cast<uint16_t>(_height - 1U)))
                [[unlikely]]
                return false;

            const uint16_t v = swapBytes ? pipcore::util::swap16(color565) : color565;
            const size_t totalPixels = static_cast<size_t>(_width) * static_cast<size_t>(_height);

            if (!_transport->fillPixels(v, totalPixels)) [[unlikely]]
                return failFromTransport(IoError::DataTx);

            return true;
        }

        void setInversion(bool enabled) noexcept
        {
            _invert = enabled;
            if (!_transport || !_initialized) [[unlikely]]
                return;
            if (!_transport->waitComplete()) [[unlikely]]
                return;

            (void)sendCommand(_invert ? pipcore::display::CmdINVON : pipcore::display::CmdINVOFF);
        }

    private:
        [[nodiscard]] bool hardReset() noexcept
        {
            if (!_transport) [[unlikely]]
                return failFromTransport(IoError::NotReady);

            if (!_transport->setRst(false)) [[unlikely]]
                return failFromTransport(IoError::Gpio);

            _transport->delayMs(10);

            if (!_transport->setRst(true)) [[unlikely]]
                return failFromTransport(IoError::Gpio);

            _transport->delayMs(120);

            return true;
        }

        [[nodiscard]] bool setRotationInternal(uint8_t rotation) noexcept
        {
            if (!_transport) [[unlikely]]
                return failFromTransport(IoError::NotReady);

            _rotation = rotation & 3U;

            const bool isOdd = (_rotation & 1U);

            _width = isOdd ? _physHeight : _physWidth;
            _height = isOdd ? _physWidth : _physHeight;
            _xStart = isOdd ? _yOffsetCfg : _xOffsetCfg;
            _yStart = isOdd ? _xOffsetCfg : _yOffsetCfg;

            using pipcore::display::MadctlBGR;
            using pipcore::display::MadctlMV;
            using pipcore::display::MadctlMX;
            using pipcore::display::MadctlMY;

            const uint8_t bgr = (_order == 1) ? MadctlBGR : 0;
            uint8_t madctl = bgr;

            if constexpr (Type == StDisplayType::ST7789)
            {
                switch (_rotation)
                {
                case 1:
                    madctl |= MadctlMX | MadctlMV;
                    break;
                case 2:
                    madctl |= MadctlMX | MadctlMY;
                    break;
                case 3:
                    madctl |= MadctlMV | MadctlMY;
                    break;
                default:
                    break;
                }
            }
            else
            {
                switch (_rotation)
                {
                case 1:
                    madctl |= MadctlMV;
                    break;
                case 2:
                    madctl |= MadctlMX | MadctlMY;
                    break;
                case 3:
                    madctl |= MadctlMX | MadctlMY | MadctlMV;
                    break;
                default:
                    break;
                }
            }

            if (!sendCommand(pipcore::display::CmdMADCTL)) [[unlikely]]
                return false;
            if (!sendBytes(&madctl, 1)) [[unlikely]]
                return false;

            return true;
        }

    private:
        uint16_t _width = 0;
        uint16_t _height = 0;
        uint16_t _physWidth = 0;
        uint16_t _physHeight = 0;

        uint8_t _rotation = 0;
        int16_t _xStart = 0;
        int16_t _yStart = 0;
        int16_t _xOffsetCfg = 0;
        int16_t _yOffsetCfg = 0;

        uint8_t _order = 0;
        bool _invert = true;
        bool _swap = false;
        bool _initialized = false;
    };
}
