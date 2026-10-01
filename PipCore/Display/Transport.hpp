#pragma once

#include <cstddef>
#include <cstdint>

namespace pipcore::display
{
    enum class IoError : uint8_t
    {
        None = 0,
        InvalidConfig,
        NotReady,
        Gpio,
        SpiInit,
        DmaAlloc,
        CmdTx,
        DataTx,
        QueueTx,
    };

    [[nodiscard]] constexpr const char *ioErrorText(IoError error) noexcept
    {
        switch (error)
        {
        case IoError::None:
            return "ok";
        case IoError::InvalidConfig:
            return "invalid config";
        case IoError::NotReady:
            return "not ready";
        case IoError::Gpio:
            return "gpio failed";
        case IoError::SpiInit:
            return "spi init failed";
        case IoError::DmaAlloc:
            return "dma alloc failed";
        case IoError::CmdTx:
            return "cmd tx failed";
        case IoError::DataTx:
            return "data tx failed";
        case IoError::QueueTx:
            return "queue tx failed";
        default:
            return "unknown io error";
        }
    }

    class Transport
    {
    public:
        Transport() = default;
        Transport(const Transport &) = delete;
        Transport &operator=(const Transport &) = delete;
        virtual ~Transport() = default;

        [[nodiscard]] virtual bool init() noexcept = 0;
        virtual void deinit() noexcept = 0;

        [[nodiscard]] virtual IoError lastError() const noexcept = 0;
        virtual void clearError() noexcept = 0;

        [[nodiscard]] virtual bool setRst(bool level) noexcept = 0;
        virtual void delayMs(uint32_t ms) noexcept = 0;

        [[nodiscard]] virtual bool write(const void *data, size_t len) noexcept = 0;
        [[nodiscard]] virtual bool writeCommand(uint8_t cmd) noexcept = 0;
        [[nodiscard]] virtual bool writePixels(const void *data, size_t len) noexcept = 0;

        [[nodiscard]] virtual bool writePixelsAsync(const void *data, size_t len) noexcept
        {
            return writePixels(data, len);
        }

        [[nodiscard]] virtual bool fillPixels(uint16_t color, size_t count) noexcept
        {
            static constexpr size_t kStage = 256;
            uint16_t stage[kStage];
            for (size_t i = 0; i < kStage; ++i)
                stage[i] = color;

            while (count != 0)
            {
                const size_t chunk = (count < kStage) ? count : kStage;
                if (!writePixels(stage, chunk))
                    return false;
                count -= chunk;
            }
            return true;
        }

        [[nodiscard]] virtual bool acquireBus() noexcept = 0;
        virtual void releaseBus() noexcept = 0;
        [[nodiscard]] virtual bool isBusAcquired() const noexcept { return false; }

        [[nodiscard]] virtual bool flush() noexcept = 0;
        [[nodiscard]] virtual bool waitComplete() noexcept = 0;
        [[nodiscard]] virtual bool waitOldest() noexcept { return waitComplete(); }

        [[nodiscard]] virtual bool writeAddrWindow(uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye) noexcept
        {
            (void)xs;
            (void)xe;
            (void)ys;
            (void)ye;
            return false;
        }

        [[nodiscard]] virtual bool supportsDirectPixels() const noexcept { return false; }

        [[nodiscard]] virtual uint8_t *directPixelsBuffer(size_t &capacity) noexcept
        {
            capacity = 0;
            return nullptr;
        }

        [[nodiscard]] virtual bool submitDirectPixels(size_t len) noexcept
        {
            (void)len;
            return false;
        }

        [[nodiscard]] virtual size_t preferredChunkBytes() const noexcept { return 0; }
    };
}
