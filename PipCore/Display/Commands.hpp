#pragma once

#include <cstdint>

#include "Config.hpp"
#include "Display/Transport.hpp"

namespace pipcore::display
{
    inline constexpr uint8_t CmdSWRESET = 0x01;
    inline constexpr uint8_t CmdSLPOUT = 0x11;
    inline constexpr uint8_t CmdNORON = 0x13;
    inline constexpr uint8_t CmdINVOFF = 0x20;
    inline constexpr uint8_t CmdINVON = 0x21;
    inline constexpr uint8_t CmdDISPON = 0x29;
    inline constexpr uint8_t CmdCASET = 0x2A;
    inline constexpr uint8_t CmdPASET = 0x2B;
    inline constexpr uint8_t CmdRAMWR = 0x2C;
    inline constexpr uint8_t CmdMADCTL = 0x36;
    inline constexpr uint8_t CmdCOLMOD = 0x3A;

    inline constexpr uint8_t Colmod16bpp = 0x55;
    inline constexpr uint8_t Colmod18bpp = 0x66;

    inline constexpr uint8_t MadctlMY = 0x80;
    inline constexpr uint8_t MadctlMX = 0x40;
    inline constexpr uint8_t MadctlMV = 0x20;
    inline constexpr uint8_t MadctlBGR = 0x08;

    class CmdWriter
    {
    public:
        [[nodiscard]] IoError lastError() const noexcept { return _lastError; }
        [[nodiscard]] const char *lastErrorText() const noexcept { return ioErrorText(_lastError); }

    protected:
        CmdWriter() = default;
        ~CmdWriter() = default;

        [[nodiscard]] bool failFromTransport(IoError fallback) noexcept
        {
            IoError err = fallback;
            if (_transport)
            {
                const IoError transportErr = _transport->lastError();
                if (transportErr != IoError::None)
                {
                    err = transportErr;
                }
            }
            _lastError = err;
            return false;
        }

        [[nodiscard]] PIPCORE_ALWAYS_INLINE bool sendCommand(uint8_t cmd) noexcept
        {
            if (!_transport)
                return failFromTransport(IoError::NotReady);
            if (_transport->writeCommand(cmd))
                return true;
            return failFromTransport(IoError::CmdTx);
        }

        [[nodiscard]] PIPCORE_ALWAYS_INLINE bool sendBytes(const void *data, size_t len) noexcept
        {
            if (!_transport)
                return failFromTransport(IoError::NotReady);
            if (_transport->write(data, len))
                return true;
            return failFromTransport(IoError::DataTx);
        }

        [[nodiscard]] PIPCORE_ALWAYS_INLINE bool sendPixels(const void *data, size_t len) noexcept
        {
            if (!_transport)
                return failFromTransport(IoError::NotReady);
            if (_transport->writePixels(data, len))
                return true;
            return failFromTransport(IoError::DataTx);
        }

        [[nodiscard]] bool writeReg(uint8_t cmd, const uint8_t *data = nullptr, size_t len = 0) noexcept
        {
            if (!sendCommand(cmd)) [[unlikely]]
                return false;
            if (len > 0 && data)
            {
                if (!sendBytes(data, len)) [[unlikely]]
                    return false;
            }
            return true;
        }

        Transport *_transport = nullptr;
        IoError _lastError = IoError::None;
    };
}
