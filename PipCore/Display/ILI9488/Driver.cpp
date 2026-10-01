#include <algorithm>

#include "Config.hpp"
#if PIPCORE_DISPLAY_ID(PIPCORE_DISPLAY) == PIPCORE_DISPLAY_TAG_ILI9488

#include "Display/ILI9488/Driver.hpp"

namespace pipcore::ili9488
{
    namespace
    {
        inline void pack16BE(uint8_t *buf, uint16_t a, uint16_t b) noexcept
        {
            buf[0] = static_cast<uint8_t>(a >> 8);
            buf[1] = static_cast<uint8_t>(a & 0xFF);
            buf[2] = static_cast<uint8_t>(b >> 8);
            buf[3] = static_cast<uint8_t>(b & 0xFF);
        }
    }

    void Driver::reset() noexcept
    {
        *this = Driver();
    }

    bool Driver::flushTransport() noexcept
    {
        if (!_transport)
            return failFromTransport(IoError::NotReady);
        if (_transport->flush())
            return true;
        return failFromTransport(IoError::QueueTx);
    }

    bool Driver::configure(Transport *transport, uint16_t width, uint16_t height, uint8_t order, bool invert, bool swap,
                           int16_t xOffset, int16_t yOffset) noexcept
    {
        if (!transport || !width || !height || xOffset < 0 || yOffset < 0)
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
        _order = (order == 1) ? 1 : 0;
        _invert = invert;
        _swap = swap;
        _initialized = false;
        _lastError = IoError::None;
        _transport->clearError();
        return true;
    }

    bool Driver::hardReset() noexcept
    {
        if (!_transport)
            return failFromTransport(IoError::NotReady);
        if (!_transport->setRst(false))
            return failFromTransport(IoError::Gpio);
        _transport->delayMs(20);
        if (!_transport->setRst(true))
            return failFromTransport(IoError::Gpio);
        _transport->delayMs(150);
        return true;
    }

    bool Driver::begin(uint8_t rotation) noexcept
    {
        _initialized = false;
        _lastError = IoError::None;

        if (!_transport || !_width || !_height)
        {
            _lastError = IoError::InvalidConfig;
            return false;
        }

        _transport->clearError();
        if (!_transport->init())
            return failFromTransport(IoError::NotReady);
        if (!hardReset())
            return false;

        if (!sendCommand(pipcore::display::CmdSWRESET)) [[unlikely]]
            return false;
        _transport->delayMs(150);

        // PVGAMCTRL: positive gamma curve
        const uint8_t e0_data[] = {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78,
                                   0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F};
        // NVGAMCTRL: negative gamma curve
        const uint8_t e1_data[] = {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45,
                                   0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F};
        // PWR1: power rails (vendor-tuned)
        const uint8_t c0_data[] = {0x17, 0x15};
        // PWR2: VGH/VGL ratio (vendor-tuned)
        const uint8_t c1_data[] = {0x41};
        // VMCTRL: VCOM voltage
        const uint8_t c5_data[] = {0x00, 0x12, 0x80};
        const uint8_t interface_format = pipcore::display::Colmod18bpp;
        // Interface mode: internal clock operation
        const uint8_t b0_data[] = {0x00};
        // Frame memory access (vendor-tuned)
        const uint8_t b1_data[] = {0xA0};
        // INVCTR: 2-dot inversion
        const uint8_t b4_data[] = {0x02};
        // FRCTRL2: frame rate 60 Hz
        const uint8_t b6_data[] = {0x02, 0x02, 0x3B};
        // GCTRL: gate rails (vendor-tuned)
        const uint8_t b7_data[] = {0xC6};
        // Adjust control 3: interface power (vendor)
        const uint8_t f7_data[] = {0xA9, 0x51, 0x2C, 0x82};

        if (!writeReg(0xE0, e0_data, sizeof(e0_data))) [[unlikely]]
            return false;
        if (!writeReg(0xE1, e1_data, sizeof(e1_data))) [[unlikely]]
            return false;
        if (!writeReg(0xC0, c0_data, sizeof(c0_data))) [[unlikely]]
            return false;
        if (!writeReg(0xC1, c1_data, sizeof(c1_data))) [[unlikely]]
            return false;
        if (!writeReg(0xC5, c5_data, sizeof(c5_data))) [[unlikely]]
            return false;
        if (!writeReg(pipcore::display::CmdCOLMOD, &interface_format, 1)) [[unlikely]]
            return false;
        if (!writeReg(0xB0, b0_data, sizeof(b0_data))) [[unlikely]]
            return false;
        if (!writeReg(0xB1, b1_data, sizeof(b1_data))) [[unlikely]]
            return false;
        if (!writeReg(0xB4, b4_data, sizeof(b4_data))) [[unlikely]]
            return false;
        if (!writeReg(0xB6, b6_data, sizeof(b6_data))) [[unlikely]]
            return false;
        if (!writeReg(0xB7, b7_data, sizeof(b7_data))) [[unlikely]]
            return false;
        if (!writeReg(0xF7, f7_data, sizeof(f7_data))) [[unlikely]]
            return false;

        _transport->delayMs(10);

        if (!sendCommand(pipcore::display::CmdSLPOUT)) [[unlikely]]
            return false;
        _transport->delayMs(120);

        if (!sendCommand(_invert ? pipcore::display::CmdINVON : pipcore::display::CmdINVOFF)) [[unlikely]]
            return false;
        _transport->delayMs(10);

        if (!sendCommand(pipcore::display::CmdDISPON)) [[unlikely]]
            return false;
        _transport->delayMs(25);

        if (!setRotationInternal(rotation)) [[unlikely]]
            return false;

        _initialized = true;
        _lastError = IoError::None;
        return true;
    }

    bool Driver::setRotation(uint8_t rotation) noexcept
    {
        if (!_transport || !_initialized)
        {
            _lastError = IoError::NotReady;
            return false;
        }
        if (!_transport->waitComplete()) [[unlikely]]
            return false;
        return setRotationInternal(rotation);
    }

    bool Driver::setRotationInternal(uint8_t rotation) noexcept
    {
        if (!_transport)
            return failFromTransport(IoError::NotReady);

        _rotation = rotation & 3U;

        using pipcore::display::MadctlBGR;
        using pipcore::display::MadctlMV;
        using pipcore::display::MadctlMX;
        using pipcore::display::MadctlMY;

        const uint8_t order = (_order == 1U) ? MadctlBGR : 0U;

        uint8_t madctl = 0;
        switch (_rotation)
        {
        case 0:
            madctl = order;
            _width = _physWidth;
            _height = _physHeight;
            _xStart = _xOffsetCfg;
            _yStart = _yOffsetCfg;
            break;
        case 1:
            madctl = MadctlMX | MadctlMV | order;
            _width = _physHeight;
            _height = _physWidth;
            _xStart = _yOffsetCfg;
            _yStart = _xOffsetCfg;
            break;
        case 2:
            madctl = MadctlMX | MadctlMY | order;
            _width = _physWidth;
            _height = _physHeight;
            _xStart = _xOffsetCfg;
            _yStart = _yOffsetCfg;
            break;
        case 3:
            madctl = MadctlMX | MadctlMY | MadctlMV | order;
            _width = _physHeight;
            _height = _physWidth;
            _xStart = _yOffsetCfg;
            _yStart = _xOffsetCfg;
            break;
        default:
            _lastError = IoError::InvalidConfig;
            return false;
        }

        if (!sendCommand(pipcore::display::CmdMADCTL))
            return false;
        if (!sendBytes(&madctl, 1))
            return false;
        return true;
    }

    bool Driver::setAddrWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) noexcept
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

        const int32_t xs32 = x0 + _xStart;
        const int32_t xe32 = x1 + _xStart;
        const int32_t ys32 = y0 + _yStart;
        const int32_t ye32 = y1 + _yStart;
        if (xs32 < 0 || ys32 < 0)
        {
            _lastError = IoError::InvalidConfig;
            return false;
        }

        uint8_t buf[4];
        const bool ownBus = !_transport->isBusAcquired();
        if (ownBus && !_transport->acquireBus())
            return failFromTransport(IoError::QueueTx);
        if (!sendCommand(pipcore::display::CmdCASET))
        {
            if (ownBus)
                _transport->releaseBus();
            return false;
        }
        pack16BE(buf, static_cast<uint16_t>(xs32), static_cast<uint16_t>(xe32));
        if (!sendBytes(buf, 4))
        {
            if (ownBus)
                _transport->releaseBus();
            return false;
        }
        if (!sendCommand(pipcore::display::CmdPASET))
        {
            if (ownBus)
                _transport->releaseBus();
            return false;
        }
        pack16BE(buf, static_cast<uint16_t>(ys32), static_cast<uint16_t>(ye32));
        if (!sendBytes(buf, 4))
        {
            if (ownBus)
                _transport->releaseBus();
            return false;
        }

        const bool ok = sendCommand(pipcore::display::CmdRAMWR);
        if (ownBus)
            _transport->releaseBus();
        return ok;
    }

    bool Driver::beginWriteWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) noexcept
    {
        if (!_transport)
            return failFromTransport(IoError::NotReady);
        const bool ownBus = !_transport->isBusAcquired();
        if (ownBus && !_transport->acquireBus())
            return failFromTransport(IoError::QueueTx);
        if (setAddrWindow(x0, y0, x1, y1))
            return true;
        if (ownBus)
            _transport->releaseBus();
        return false;
    }

    void Driver::endWrite() noexcept
    {
        if (!_transport)
            return;
        (void)flushTransport();
        _transport->releaseBus();
    }

    bool Driver::writePixels666(const uint8_t *bytes, size_t len) noexcept
    {
        if (!_transport || !_initialized || !bytes || !len)
        {
            _lastError = (_transport && _initialized) ? IoError::InvalidConfig : IoError::NotReady;
            return false;
        }
        return sendPixels(bytes, len);
    }

    PIPCORE_HOT bool Driver::fillScreen565(uint16_t color565) noexcept
    {
        if (!_transport || !_initialized || !_width || !_height)
        {
            _lastError = (_transport && _initialized) ? IoError::InvalidConfig : IoError::NotReady;
            return false;
        }
        if (!beginWriteWindow(0, 0, static_cast<uint16_t>(_width - 1U), static_cast<uint16_t>(_height - 1U)))
            return false;

        constexpr size_t ChunkPixels = 128;
        uint8_t tmp[ChunkPixels * 3U];
        const uint8_t r = static_cast<uint8_t>((color565 >> 8) & 0xF8);
        const uint8_t g = static_cast<uint8_t>((color565 >> 3) & 0xFC);
        const uint8_t b = static_cast<uint8_t>((color565 << 3) & 0xF8);
        for (size_t i = 0; i < ChunkPixels; ++i)
        {
            const size_t base = i * 3U;
            tmp[base] = r;
            tmp[base + 1U] = g;
            tmp[base + 2U] = b;
        }

        size_t remaining = static_cast<size_t>(_width) * static_cast<size_t>(_height);
        while (remaining)
        {
            const size_t pixels = std::min(remaining, ChunkPixels);
            if (!sendPixels(tmp, pixels * 3U))
            {
                endWrite();
                return false;
            }
            remaining -= pixels;
        }

        endWrite();
        return true;
    }
}

#endif
