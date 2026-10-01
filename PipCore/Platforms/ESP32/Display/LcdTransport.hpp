#pragma once

#include <cstdint>
#include <cstddef>
#include <driver/spi_master.h>

#include "Config.hpp"
#include "Display/Transport.hpp"

#if !PIPCORE_TARGET_ESP32
#error "pipcore::esp32::LcdTransport requires ESP32"
#endif

namespace pipcore::esp32
{

    class LcdTransport final : public display::Transport
    {
    public:
        enum class Mode : uint8_t
        {
            DriverCs,
            DmaStream,
        };

        LcdTransport() = default;
        ~LcdTransport();

        void configure(int8_t mosi, int8_t sclk, int8_t cs, int8_t dc, int8_t rst, uint32_t hz = 0,
                       Mode mode = Mode::DriverCs) noexcept;

        [[nodiscard]] bool init() noexcept override;
        void deinit() noexcept override;
        [[nodiscard]] display::IoError lastError() const noexcept override { return _lastError; }
        void clearError() noexcept override { _lastError = display::IoError::None; }
        [[nodiscard]] bool setRst(bool level) noexcept override;
        void delayMs(uint32_t ms) noexcept override;
        [[nodiscard]] bool write(const void *data, size_t len) noexcept override;
        [[nodiscard]] bool writeCommand(uint8_t cmd) noexcept override;
        [[nodiscard]] bool writePixels(const void *data, size_t len) noexcept override;
        [[nodiscard]] bool acquireBus() noexcept override;
        void releaseBus() noexcept override;
        [[nodiscard]] bool isBusAcquired() const noexcept override { return _busAcquired; }
        [[nodiscard]] bool flush() noexcept override;
        [[nodiscard]] bool waitComplete() noexcept override;

        [[nodiscard]] inline bool IRAM_ATTR PIPCORE_ALWAYS_INLINE
        writePixelsAsync(const void *data, size_t len) noexcept override
        {
            return writePixelsImpl(data, len);
        }
        [[nodiscard]] bool fillPixels(uint16_t color, size_t count) noexcept override;
        [[nodiscard]] bool waitOldest() noexcept override;
        [[nodiscard]] bool writeAddrWindow(uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye) noexcept override;

        [[nodiscard]] bool supportsDirectPixels() const noexcept override { return _mode == Mode::DmaStream && _useDma; }
        [[nodiscard]] uint8_t *directPixelsBuffer(size_t &capacity) noexcept override;
        [[nodiscard]] bool submitDirectPixels(size_t len) noexcept override;
        [[nodiscard]] size_t preferredChunkBytes() const noexcept override
        {
            return DmaChunkBytes;
        }

    private:
        [[nodiscard]] bool initSpi() noexcept;
        [[nodiscard]] bool initDmaPath() noexcept;
        [[nodiscard]] bool initGpioOutputs(bool includeCs) noexcept;
        void resetState() noexcept;
        [[nodiscard]] bool drainQueue() noexcept;
        [[nodiscard]] bool waitQueued() noexcept;
        [[nodiscard]] bool flushQueued() noexcept;
        [[nodiscard]] bool popOldest() noexcept;
        [[nodiscard]] bool fail(display::IoError error) noexcept;
        inline void setCsCached(int level) noexcept;
        inline void setDcCached(int level) noexcept;
        [[nodiscard]] bool queueAsync(const void *data, size_t bytes, uint32_t flags, uint8_t slotStage) noexcept;
        [[nodiscard]] bool writePixelsImpl(const void *data, size_t len) noexcept;

        static constexpr size_t HardwareMaxDmaBytes = 32768U;
        static constexpr size_t CsStageBytes = 4096U;
        static constexpr size_t DmaChunkBytes = 2304U;
        static constexpr int MaxAsyncTrans = 4;
        static constexpr int MaxDmaBufs = 2;

        Mode _mode = Mode::DriverCs;

        int8_t _pinMosi = -1;
        int8_t _pinSclk = -1;
        int8_t _pinCs = -1;
        int8_t _pinDc = -1;
        int8_t _pinRst = -1;
        uint32_t _hz = 80000000U;

        spi_device_handle_t _dev = nullptr;
        int _spiHost = 2;
        bool _busOwned = false;
        uint8_t *_stage = nullptr;
        uint8_t *_dmaBuf[MaxDmaBufs] = {nullptr, nullptr};
        bool _busAcquired = false;
        bool _initialized = false;
        display::IoError _lastError = display::IoError::None;

        spi_transaction_t _asyncTrans[MaxAsyncTrans]{};
        spi_transaction_t _addrTrans[5]{};
        int _asyncNext = 0;
        int _asyncInFlight = 0;
        int _stageBufNext = 0;
        bool _bufInFlight[2] = {};
        uint8_t _slotBuf[MaxAsyncTrans] = {};
        uint16_t _lastXs = 0xFFFF;
        uint16_t _lastXe = 0xFFFF;
        uint16_t _lastYs = 0xFFFF;
        uint16_t _lastYe = 0xFFFF;

        spi_transaction_t *_trans[MaxDmaBufs] = {nullptr, nullptr};
        bool _transInFlight[MaxDmaBufs] = {false, false};
        int _dmaNext = 0;
        int _dmaInflight = 0;
        int8_t _csLevel = -1;
        int8_t _dcLevel = -1;
        bool _useDma = false;
    };
}
