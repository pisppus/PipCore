#include "Config.hpp"
#if PIPCORE_TARGET_ESP32
#include <esp_heap_caps.h>
#include <esp_attr.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <hal/gpio_ll.h>
#include <esp_rom_gpio.h>
#include <esp_rom_sys.h>
#if __has_include(<esp_memory_utils.h>)
#include <esp_memory_utils.h>
#endif
#if __has_include(<soc/soc_memory_layout.h>)
#include <soc/soc_memory_layout.h>
#endif
#if __has_include(<soc/spi_periph.h>)
#include <soc/spi_periph.h>
#endif
#if __has_include(<soc/soc_caps.h>)
#include <soc/soc_caps.h>
#endif
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <algorithm>

#include "Platforms/ESP32/Display/LcdTransport.hpp"
#include "Display/Commands.hpp"
#include "Core/Memory.hpp"

namespace pipcore::esp32
{
    namespace
    {
        [[nodiscard]] inline constexpr bool isPinValid(int8_t pin) noexcept
        {
            return pin >= 0;
        }

        inline void IRAM_ATTR fastFill32(uint32_t *dest, size_t words, uint32_t value) noexcept
        {
            size_t blocks = words >> 3;
            while (blocks--)
            {
                dest[0] = value;
                dest[1] = value;
                dest[2] = value;
                dest[3] = value;
                dest[4] = value;
                dest[5] = value;
                dest[6] = value;
                dest[7] = value;
                dest += 8;
            }

            size_t remainder = words & 7U;
            while (remainder--)
            {
                *dest++ = value;
            }
        }

        [[nodiscard]] inline bool isDmaCapable(const void *p) noexcept
        {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
            return esp_ptr_internal(p) || esp_ptr_dma_ext_capable(p);
#else
            return esp_ptr_dma_capable(p);
#endif
        }

#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
#define PIPCORE_GPIO_FAST_OUT 1
#endif

        inline void IRAM_ATTR gpio_fast_write_high(int8_t pin) noexcept
        {
#if PIPCORE_GPIO_FAST_OUT
            if (pin < 32) [[likely]]
            {
                GPIO.out_w1ts = (1UL << pin);
                return;
            }
#endif
            gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), 1);
        }

        inline void IRAM_ATTR gpio_fast_write_low(int8_t pin) noexcept
        {
#if PIPCORE_GPIO_FAST_OUT
            if (pin < 32) [[likely]]
            {
                GPIO.out_w1tc = (1UL << pin);
                return;
            }
#endif
            gpio_ll_set_level(&GPIO, static_cast<gpio_num_t>(pin), 0);
        }

        void IRAM_ATTR lcd_spi_pre_cb(spi_transaction_t *t)
        {
            const uint32_t packed = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t->user));
            const int8_t pin = static_cast<int8_t>(packed >> 8);
            const uint8_t level = static_cast<uint8_t>(packed);

            if (pin >= 0)
            {
                if (level)
                    gpio_fast_write_high(pin);
                else
                    gpio_fast_write_low(pin);
            }
        }

        [[nodiscard]] inline void *packDcInfo(int8_t pin, uint8_t level) noexcept
        {
            const uint32_t packed = (static_cast<uint32_t>(static_cast<uint8_t>(pin)) << 8) | level;
            return reinterpret_cast<void *>(static_cast<uintptr_t>(packed));
        }

        [[nodiscard]] constexpr int8_t resolveDefaultMosi() noexcept
        {
#if defined(CONFIG_IDF_TARGET_ESP32)
            return 13;
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
            return 11;
#elif defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
            return 7;
#else
            return -1;
#endif
        }

        [[nodiscard]] constexpr int8_t resolveDefaultSclk() noexcept
        {
#if defined(CONFIG_IDF_TARGET_ESP32)
            return 14;
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3)
            return 12;
#elif defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32C3) || \
    defined(CONFIG_IDF_TARGET_ESP32C6) || defined(CONFIG_IDF_TARGET_ESP32H2)
            return 6;
#else
            return -1;
#endif
        }

        [[nodiscard]] constexpr int8_t resolveDefaultCs() noexcept
        {
#if defined(CONFIG_IDF_TARGET_ESP32)
            return 15;
#elif defined(CONFIG_IDF_TARGET_ESP32S2) || defined(CONFIG_IDF_TARGET_ESP32S3) ||                                     \
    defined(CONFIG_IDF_TARGET_ESP32C2) || defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6) || \
    defined(CONFIG_IDF_TARGET_ESP32H2)
            return 10;
#else
            return -1;
#endif
        }

        [[nodiscard]] inline int8_t getSpi2IomuxMosi() noexcept
        {
#if __has_include(<soc/spi_periph.h>)
            return static_cast<int8_t>(spi_periph_signal[SPI2_HOST].spid_iomux_pin);
#else
            return -1;
#endif
        }
        [[nodiscard]] inline int8_t getSpi2IomuxSclk() noexcept
        {
#if __has_include(<soc/spi_periph.h>)
            return static_cast<int8_t>(spi_periph_signal[SPI2_HOST].spiclk_iomux_pin);
#else
            return -1;
#endif
        }

        [[nodiscard]] inline int8_t getSpi2IomuxCs0() noexcept
        {
#if __has_include(<soc/spi_periph.h>)
            return static_cast<int8_t>(spi_periph_signal[SPI2_HOST].spics0_iomux_pin);
#else
            return -1;
#endif
        }

        [[nodiscard]] inline bool trySpiBusInitialize(int host, spi_bus_config_t &bus) noexcept
        {
            return spi_bus_initialize(static_cast<spi_host_device_t>(host), &bus, SPI_DMA_CH_AUTO) == ESP_OK;
        }

        [[nodiscard]] bool spiBusAcquireAny(int &outHost, spi_bus_config_t &bus) noexcept
        {
            if (trySpiBusInitialize(SPI2_HOST, bus))
            {
                outHost = SPI2_HOST;
                return true;
            }
#if SOC_SPI_PERIPH_NUM >= 3
            if (trySpiBusInitialize(SPI3_HOST, bus))
            {
                outHost = SPI3_HOST;
                return true;
            }
#endif
            return false;
        }
    }

    void LcdTransport::configure(int8_t mosi, int8_t sclk, int8_t cs, int8_t dc, int8_t rst, uint32_t hz,
                                 Mode mode) noexcept
    {
        deinit();

        _mode = mode;

        if (_mode == Mode::DmaStream)
        {
            _pinMosi = isPinValid(mosi) ? mosi : getSpi2IomuxMosi();
            _pinSclk = isPinValid(sclk) ? sclk : getSpi2IomuxSclk();
            _pinCs = isPinValid(cs) ? cs : getSpi2IomuxCs0();
            _hz = hz ? hz : 60000000U;
        }
        else
        {
            _pinMosi = isPinValid(mosi) ? mosi : resolveDefaultMosi();
            _pinSclk = isPinValid(sclk) ? sclk : resolveDefaultSclk();
            _pinCs = isPinValid(cs) ? cs : resolveDefaultCs();
            _hz = hz ? hz : 80000000U;
        }

        _pinDc = dc;
        _pinRst = rst;

        resetState();
    }

    void LcdTransport::resetState() noexcept
    {
        for (int i = 0; i < MaxDmaBufs; ++i)
        {
            _dmaBuf[i] = nullptr;
            _trans[i] = nullptr;
            _transInFlight[i] = false;
        }
        _dmaNext = 0;
        _dmaInflight = 0;
        _csLevel = -1;
        _dcLevel = -1;
        _busAcquired = false;
        _initialized = false;
        _useDma = false;
        _lastError = display::IoError::None;

        std::memset(_asyncTrans, 0, sizeof(_asyncTrans));
        _asyncNext = 0;
        _stageBufNext = 0;
        _asyncInFlight = 0;
        _bufInFlight[0] = _bufInFlight[1] = false;

        _lastXs = 0xFFFF;
        _lastXe = 0xFFFF;
        _lastYs = 0xFFFF;
        _lastYe = 0xFFFF;
    }

    LcdTransport::~LcdTransport()
    {
        LcdTransport::deinit();
    }

    bool LcdTransport::fail(display::IoError error) noexcept
    {
        _lastError = error;
        return false;
    }

    bool LcdTransport::init() noexcept
    {
        clearError();
        if (_initialized)
            return true;

        if (_mode == Mode::DmaStream)
        {
            if (!isPinValid(_pinMosi) || !isPinValid(_pinSclk) || !isPinValid(_pinDc)) [[unlikely]]
                return fail(display::IoError::InvalidConfig);

            if (!initSpi()) [[unlikely]]
                return false;

            if (!initGpioOutputs(true)) [[unlikely]]
                return false;

            if (isPinValid(_pinCs))
                setCsCached(1);

            _initialized = true;
            return true;
        }

        if (!isPinValid(_pinDc)) [[unlikely]]
            return fail(display::IoError::InvalidConfig);

        if (!initSpi()) [[unlikely]]
            return false;

        if (!initGpioOutputs(false)) [[unlikely]]
            return false;

        _initialized = true;
        return true;
    }

    bool LcdTransport::initGpioOutputs(bool includeCs) noexcept
    {
        gpio_config_t io{};
        io.intr_type = GPIO_INTR_DISABLE;
        io.mode = GPIO_MODE_OUTPUT;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pin_bit_mask = 1ULL << static_cast<uint8_t>(_pinDc);
        if (includeCs && isPinValid(_pinCs))
            io.pin_bit_mask |= 1ULL << static_cast<uint8_t>(_pinCs);
        if (isPinValid(_pinRst))
            io.pin_bit_mask |= 1ULL << static_cast<uint8_t>(_pinRst);

        if (gpio_config(&io) != ESP_OK) [[unlikely]]
        {
            deinit();
            return fail(display::IoError::Gpio);
        }
        return true;
    }

    void LcdTransport::deinit() noexcept
    {
        if (_dev)
        {
            (void)flush();
            spi_bus_remove_device(_dev);
            _dev = nullptr;
        }

        if (_busOwned)
        {
            spi_bus_free(static_cast<spi_host_device_t>(_spiHost));
            _busOwned = false;
        }
        _spiHost = SPI2_HOST;

        if (isPinValid(_pinDc))
            gpio_reset_pin(static_cast<gpio_num_t>(_pinDc));
        if (isPinValid(_pinCs))
            gpio_reset_pin(static_cast<gpio_num_t>(_pinCs));
        if (isPinValid(_pinRst))
            gpio_reset_pin(static_cast<gpio_num_t>(_pinRst));

        if (_stage)
        {
            void *freed = _stage;
            heap_caps_free(_stage);
            _stage = nullptr;
            pipcore::debug::memoryEvent(pipcore::debug::MemoryEvent::Free, "lcd.stage.free", freed, CsStageBytes * 2,
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        }

        for (int i = 0; i < MaxDmaBufs; ++i)
        {
            if (_dmaBuf[i])
            {
                void *freed = _dmaBuf[i];
                heap_caps_free(_dmaBuf[i]);
                _dmaBuf[i] = nullptr;
                pipcore::debug::memoryEvent(pipcore::debug::MemoryEvent::Free, "lcd.dma.free", freed, DmaChunkBytes,
                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
            }

            if (_trans[i])
            {
                void *freed = _trans[i];
                heap_caps_free(_trans[i]);
                _trans[i] = nullptr;
                pipcore::debug::memoryEvent(pipcore::debug::MemoryEvent::Free, "lcd.trans.free", freed,
                                            sizeof(spi_transaction_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            }
        }

        resetState();
    }

    bool LcdTransport::initSpi() noexcept
    {
        if (_dev)
            return true;

        if (_mode == Mode::DmaStream)
        {
            if (initDmaPath())
                return true;
            deinit();
            return fail(display::IoError::DmaAlloc);
        }

        if (!isPinValid(_pinMosi) || !isPinValid(_pinSclk)) [[unlikely]]
            return fail(display::IoError::InvalidConfig);

        spi_bus_config_t bus{};
        bus.mosi_io_num = _pinMosi;
        bus.miso_io_num = -1;
        bus.sclk_io_num = _pinSclk;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = static_cast<int>(HardwareMaxDmaBytes);
#if defined(CONFIG_SPI_MASTER_ISR_IN_IRAM)
        bus.intr_flags = ESP_INTR_FLAG_IRAM;
#endif

        if (!spiBusAcquireAny(_spiHost, bus)) [[unlikely]]
            return fail(display::IoError::SpiInit);
        _busOwned = true;

        spi_device_interface_config_t dev{};
        dev.mode = 0;
        dev.clock_speed_hz = static_cast<int>(_hz);
        dev.spics_io_num = isPinValid(_pinCs) ? _pinCs : -1;
        dev.flags = SPI_DEVICE_NO_DUMMY | SPI_DEVICE_HALFDUPLEX;
        dev.queue_size = MaxAsyncTrans;
        dev.pre_cb = lcd_spi_pre_cb;

        spi_device_handle_t h = nullptr;
        if (spi_bus_add_device(static_cast<spi_host_device_t>(_spiHost), &dev, &h) != ESP_OK) [[unlikely]]
        {
            spi_bus_free(static_cast<spi_host_device_t>(_spiHost));
            _busOwned = false;
            return fail(display::IoError::SpiInit);
        }
        _dev = h;

        _stage = static_cast<uint8_t *>(
            heap_caps_aligned_alloc(16, CsStageBytes * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        pipcore::debug::memoryEvent(_stage ? pipcore::debug::MemoryEvent::Alloc : pipcore::debug::MemoryEvent::AllocFail,
                                    "lcd.stage.alloc", _stage, CsStageBytes * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!_stage)
        {
            deinit();
            return fail(display::IoError::DmaAlloc);
        }

        std::memset(_asyncTrans, 0, sizeof(_asyncTrans));
        for (int i = 0; i < MaxAsyncTrans; ++i)
            _asyncTrans[i].user = packDcInfo(_pinDc, 1);

        std::memset(_addrTrans, 0, sizeof(_addrTrans));

        _addrTrans[0].flags = SPI_TRANS_USE_TXDATA;
        _addrTrans[0].length = 8;
        _addrTrans[0].tx_data[0] = pipcore::display::CmdCASET;
        _addrTrans[0].user = packDcInfo(_pinDc, 0);

        _addrTrans[1].flags = SPI_TRANS_USE_TXDATA;
        _addrTrans[1].length = 32;
        _addrTrans[1].user = packDcInfo(_pinDc, 1);

        _addrTrans[2].flags = SPI_TRANS_USE_TXDATA;
        _addrTrans[2].length = 8;
        _addrTrans[2].tx_data[0] = pipcore::display::CmdPASET;
        _addrTrans[2].user = packDcInfo(_pinDc, 0);

        _addrTrans[3].flags = SPI_TRANS_USE_TXDATA;
        _addrTrans[3].length = 32;
        _addrTrans[3].user = packDcInfo(_pinDc, 1);

        _addrTrans[4].flags = SPI_TRANS_USE_TXDATA;
        _addrTrans[4].length = 8;
        _addrTrans[4].tx_data[0] = pipcore::display::CmdRAMWR;
        _addrTrans[4].user = packDcInfo(_pinDc, 0);

        _busAcquired = false;
        _asyncNext = 0;
        _stageBufNext = 0;
        _asyncInFlight = 0;

        _lastXs = 0xFFFF;
        _lastXe = 0xFFFF;
        _lastYs = 0xFFFF;
        _lastYe = 0xFFFF;

        clearError();
        return true;
    }

    bool LcdTransport::initDmaPath() noexcept
    {
        spi_bus_config_t bus{};
        bus.mosi_io_num = _pinMosi;
        bus.miso_io_num = -1;
        bus.sclk_io_num = _pinSclk;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;
        bus.max_transfer_sz = static_cast<int>(DmaChunkBytes);
#if defined(CONFIG_SPI_MASTER_ISR_IN_IRAM)
        bus.intr_flags = ESP_INTR_FLAG_IRAM;
#endif
        if (!spiBusAcquireAny(_spiHost, bus))
            return fail(display::IoError::SpiInit);
        _busOwned = true;

        spi_device_interface_config_t dev{};
        dev.mode = 0;
        dev.clock_speed_hz = static_cast<int>(_hz);
        dev.spics_io_num = -1;
        dev.queue_size = 2;
        dev.flags = SPI_DEVICE_NO_DUMMY | SPI_DEVICE_HALFDUPLEX;

        spi_device_handle_t h = nullptr;
        if (spi_bus_add_device(static_cast<spi_host_device_t>(_spiHost), &dev, &h) != ESP_OK)
        {
            spi_bus_free(static_cast<spi_host_device_t>(_spiHost));
            _busOwned = false;
            return fail(display::IoError::SpiInit);
        }
        _dev = h;

        for (int i = 0; i < MaxDmaBufs; ++i)
        {
            _dmaBuf[i] =
                static_cast<uint8_t *>(heap_caps_aligned_alloc(16, DmaChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
            pipcore::debug::memoryEvent(
                _dmaBuf[i] ? pipcore::debug::MemoryEvent::Alloc : pipcore::debug::MemoryEvent::AllocFail, "spi.dma.alloc",
                _dmaBuf[i], DmaChunkBytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
            if (!_dmaBuf[i])
                return fail(display::IoError::DmaAlloc);

            _trans[i] = static_cast<spi_transaction_t *>(
                heap_caps_calloc(1, sizeof(spi_transaction_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
            pipcore::debug::memoryEvent(
                _trans[i] ? pipcore::debug::MemoryEvent::Alloc : pipcore::debug::MemoryEvent::AllocFail, "spi.trans.alloc",
                _trans[i], sizeof(spi_transaction_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!_trans[i])
                return fail(display::IoError::DmaAlloc);
        }

        _useDma = true;
        clearError();
        return true;
    }

    inline void LcdTransport::setCsCached(int level) noexcept
    {
        if (!isPinValid(_pinCs) || _csLevel == level)
            return;
        if (level)
            gpio_fast_write_high(_pinCs);
        else
            gpio_fast_write_low(_pinCs);
        _csLevel = static_cast<int8_t>(level);
    }

    inline void LcdTransport::setDcCached(int level) noexcept
    {
        if (_dcLevel == level)
            return;
        if (level)
            gpio_fast_write_high(_pinDc);
        else
            gpio_fast_write_low(_pinDc);
        _dcLevel = static_cast<int8_t>(level);
    }

    bool LcdTransport::setRst(bool level) noexcept
    {
        if (isPinValid(_pinRst))
        {
            if (level)
                gpio_fast_write_high(_pinRst);
            else
                gpio_fast_write_low(_pinRst);
        }
        return true;
    }

    void LcdTransport::delayMs(uint32_t ms) noexcept
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    bool IRAM_ATTR LcdTransport::writeCommand(uint8_t cmd) noexcept
    {
        if (!_dev) [[unlikely]]
            return fail(display::IoError::NotReady);

        if (_mode == Mode::DmaStream)
        {
            if (!flushQueued())
                return false;
            const bool ownBus = !_busAcquired;
            if (ownBus && !acquireBus())
                return false;
            setCsCached(0);
            setDcCached(0);

            spi_transaction_t t{};
            t.flags = SPI_TRANS_USE_TXDATA;
            t.length = 8;
            t.tx_data[0] = cmd;
            if (spi_device_polling_transmit(_dev, &t) != ESP_OK)
            {
                if (ownBus)
                    releaseBus();
                return fail(display::IoError::CmdTx);
            }
            if (ownBus)
                releaseBus();
            return true;
        }

        if (_asyncInFlight > 0)
        {
            if (!drainQueue()) [[unlikely]]
                return false;
        }

        spi_transaction_t t{};
        t.flags = SPI_TRANS_USE_TXDATA;
        t.length = 8;
        t.user = packDcInfo(_pinDc, 0);
        t.tx_data[0] = cmd;

        if (spi_device_polling_transmit(_dev, &t) != ESP_OK) [[unlikely]]
            return fail(display::IoError::CmdTx);

        return true;
    }

    bool IRAM_ATTR LcdTransport::write(const void *data, size_t len) noexcept
    {
        if (!len || !_dev) [[unlikely]]
            return fail(display::IoError::NotReady);

        if (_mode == Mode::DmaStream)
        {
            if (!flushQueued())
                return false;
            const bool ownBus = !_busAcquired;
            if (ownBus && !acquireBus())
                return false;
            setCsCached(0);
            setDcCached(1);

            spi_transaction_t t{};
            if (len <= 4U)
            {
                t.flags = SPI_TRANS_USE_TXDATA;
                t.length = static_cast<int>(len * 8U);
                std::memcpy(t.tx_data, data, len);
            }
            else
            {
                t.length = static_cast<int>(len * 8U);
                t.tx_buffer = data;
            }
            if (spi_device_polling_transmit(_dev, &t) != ESP_OK)
            {
                if (ownBus)
                    releaseBus();
                return fail(display::IoError::DataTx);
            }
            if (ownBus)
                releaseBus();
            return true;
        }

        if (_asyncInFlight > 0)
        {
            if (!drainQueue()) [[unlikely]]
                return false;
        }

        spi_transaction_t t{};
        t.user = packDcInfo(_pinDc, 1);
        t.length = static_cast<int>(len << 3);

        if (len <= 4U)
        {
            t.flags = SPI_TRANS_USE_TXDATA;
            std::memcpy(t.tx_data, data, len);
        }
        else
        {
            t.tx_buffer = data;
        }

        if (spi_device_polling_transmit(_dev, &t) != ESP_OK) [[unlikely]]
            return fail(display::IoError::DataTx);

        return true;
    }

    bool IRAM_ATTR LcdTransport::acquireBus() noexcept
    {
        if (_mode == Mode::DmaStream)
        {
            if (!_dev)
                return fail(display::IoError::NotReady);
            if (_busAcquired)
                return true;
            if (spi_device_acquire_bus(_dev, portMAX_DELAY) != ESP_OK)
                return fail(display::IoError::QueueTx);
            _busAcquired = true;
            return true;
        }

        _busAcquired = true;
        return true;
    }

    void LcdTransport::releaseBus() noexcept
    {
        if (_mode == Mode::DmaStream)
        {
            if (!_dev || !_busAcquired)
                return;
            setCsCached(1);
            spi_device_release_bus(_dev);
            _busAcquired = false;
            return;
        }

        _busAcquired = false;
    }

    bool IRAM_ATTR LcdTransport::popOldest() noexcept
    {
        if (_asyncInFlight <= 0)
            return true;

        spi_transaction_t *r = nullptr;

        esp_err_t err = spi_device_get_trans_result(_dev, &r, portMAX_DELAY);

        if (err != ESP_OK || !r) [[unlikely]]
        {
            _asyncNext = 0;
            _asyncInFlight = 0;
            _stageBufNext = 0;
            _bufInFlight[0] = false;
            _bufInFlight[1] = false;
            return fail(display::IoError::QueueTx);
        }

        const int slot = static_cast<int>(r - _asyncTrans);
        if (slot >= 0 && slot < MaxAsyncTrans)
        {
            const uint8_t buf = _slotBuf[slot];
            if (buf != 0U)
                _bufInFlight[buf - 1U] = false;
            _slotBuf[slot] = 0U;
        }
        _asyncInFlight--;
        return true;
    }

    bool IRAM_ATTR LcdTransport::waitOldest() noexcept
    {
        if (_mode == Mode::DmaStream)
            return waitQueued();
        return popOldest();
    }

    bool IRAM_ATTR LcdTransport::drainQueue() noexcept
    {
        if (!_dev) [[unlikely]]
            return true;

        if (_mode == Mode::DmaStream)
            return flushQueued();

        bool success = true;

        while (_asyncInFlight > 0)
        {
            if (!popOldest())
                success = false;
        }

        _asyncNext = 0;
        _stageBufNext = 0;

        return success;
    }

    bool IRAM_ATTR LcdTransport::waitQueued() noexcept
    {
        if (_dmaInflight <= 0 || !_dev)
            return true;

        spi_transaction_t *r = nullptr;
        const esp_err_t err = spi_device_get_trans_result(_dev, &r, portMAX_DELAY);
        if (err != ESP_OK || !r)
        {
            _transInFlight[0] = false;
            _transInFlight[1] = false;
            _dmaInflight = 0;
            return fail(display::IoError::QueueTx);
        }

        if (r == _trans[0])
            _transInFlight[0] = false;
        else if (r == _trans[1])
            _transInFlight[1] = false;
        else
        {
            _transInFlight[0] = false;
            _transInFlight[1] = false;
            _dmaInflight = 0;
            return fail(display::IoError::QueueTx);
        }

        --_dmaInflight;
        return true;
    }

    bool LcdTransport::flushQueued() noexcept
    {
        while (_dmaInflight > 0)
        {
            if (!waitQueued())
                return false;
        }
        return true;
    }

    bool IRAM_ATTR LcdTransport::writeAddrWindow(uint16_t xs, uint16_t xe, uint16_t ys, uint16_t ye) noexcept
    {
        if (_mode != Mode::DriverCs)
            return false;

        if (_asyncInFlight > 0)
        {
            if (!drainQueue()) [[unlikely]]
                return false;
        }

        const bool ownBus = !_busAcquired;
        if (!acquireBus()) [[unlikely]]
            return false;

        if (xs == _lastXs && xe == _lastXe && ys == _lastYs && ye == _lastYe)
        {
            spi_device_handle_t handle = _dev;
            if (spi_device_polling_transmit(handle, &_addrTrans[4]) != ESP_OK) [[unlikely]]
            {
                if (ownBus)
                    releaseBus();
                return fail(display::IoError::CmdTx);
            }

            return true;
        }

        _lastXs = xs;
        _lastXe = xe;
        _lastYs = ys;
        _lastYe = ye;

        _addrTrans[1].tx_data[0] = xs >> 8;
        _addrTrans[1].tx_data[1] = xs & 0xFF;
        _addrTrans[1].tx_data[2] = xe >> 8;
        _addrTrans[1].tx_data[3] = xe & 0xFF;

        _addrTrans[3].tx_data[0] = ys >> 8;
        _addrTrans[3].tx_data[1] = ys & 0xFF;
        _addrTrans[3].tx_data[2] = ye >> 8;
        _addrTrans[3].tx_data[3] = ye & 0xFF;

        spi_device_handle_t handle = _dev;

        for (int i = 0; i < 5; ++i)
        {
            if (spi_device_polling_transmit(handle, &_addrTrans[i]) != ESP_OK) [[unlikely]]
            {
                if (ownBus)
                    releaseBus();
                return fail(display::IoError::CmdTx);
            }
        }

        return true;
    }

    bool IRAM_ATTR LcdTransport::queueAsync(const void *data, size_t bytes, uint32_t flags,
                                            uint8_t slotStage) noexcept
    {
        spi_transaction_t *t = &_asyncTrans[_asyncNext];
        t->flags = flags;
        t->length = static_cast<int>(bytes << 3);
        t->rxlength = 0;
        t->tx_buffer = data;

        t->user = packDcInfo(_pinDc, 1);

        const esp_err_t err = spi_device_queue_trans(_dev, t, portMAX_DELAY);
        if (err != ESP_OK) [[unlikely]]
            return fail(display::IoError::QueueTx);

        _slotBuf[_asyncNext] = slotStage;
        _asyncNext = (_asyncNext + 1) % MaxAsyncTrans;
        _asyncInFlight++;
        return true;
    }

    bool IRAM_ATTR LcdTransport::writePixelsImpl(const void *data, size_t len) noexcept
    {
        if (!len || !_dev) [[unlikely]]
            return fail(display::IoError::NotReady);

        if (!acquireBus()) [[unlikely]]
            return false;

        const uint8_t *p = static_cast<const uint8_t *>(data);
        size_t remaining = len;

        const bool directDma = isDmaCapable(p) && ((reinterpret_cast<uintptr_t>(p) & 3U) == 0U);

        if (directDma)
        {
            if (remaining <= HardwareMaxDmaBytes) [[likely]]
            {
                while (_asyncInFlight >= MaxAsyncTrans)
                {
                    if (!waitOldest()) [[unlikely]]
                        return false;
                }

                return queueAsync(p, remaining, 0U, 0U);
            }

            while (remaining > 0)
            {
                while (_asyncInFlight >= MaxAsyncTrans)
                {
                    if (!waitOldest()) [[unlikely]]
                        return false;
                }

                const size_t chunk = std::min(remaining, HardwareMaxDmaBytes);
                if (!queueAsync(p, chunk, 0U, 0U))
                    return false;

                p += chunk;
                remaining -= chunk;
            }
            return true;
        }

        if (!_stage) [[unlikely]]
            return fail(display::IoError::NotReady);

        if (remaining <= CsStageBytes) [[likely]]
        {
            while (_bufInFlight[_stageBufNext])
            {
                if (!popOldest())
                    return false;
            }

            const int bufIdx = _stageBufNext;
            _stageBufNext ^= 1;

            uint8_t *stage = _stage + bufIdx * CsStageBytes;
            std::memcpy(stage, p, remaining);

            if (!queueAsync(stage, remaining, 0U, static_cast<uint8_t>(bufIdx + 1U)))
                return false;
            _bufInFlight[bufIdx] = true;
            return true;
        }

        while (remaining > 0)
        {
            while (_bufInFlight[_stageBufNext])
            {
                if (!popOldest())
                    return false;
            }

            const int bufIdx = _stageBufNext;
            _stageBufNext ^= 1;

            const size_t chunk = std::min(remaining, CsStageBytes);
            uint8_t *stage = _stage + bufIdx * CsStageBytes;
            std::memcpy(stage, p, chunk);

            if (!queueAsync(stage, chunk, 0U, static_cast<uint8_t>(bufIdx + 1U)))
                return false;
            _bufInFlight[bufIdx] = true;

            p += chunk;
            remaining -= chunk;
        }
        return true;
    }

    bool IRAM_ATTR LcdTransport::writePixels(const void *data, size_t len) noexcept
    {
        if (!len || !_dev) [[unlikely]]
            return fail(display::IoError::NotReady);

        if (_mode != Mode::DmaStream)
            return writePixelsImpl(data, len);

        const bool ownBus = !_busAcquired;
        setCsCached(0);
        setDcCached(1);
        if (ownBus && !acquireBus())
            return false;

        if (!_dmaBuf[0] || !_dmaBuf[1] || !_trans[0] || !_trans[1])
        {
            if (ownBus)
                releaseBus();
            return fail(display::IoError::NotReady);
        }

        const uint8_t *p = static_cast<const uint8_t *>(data);
        size_t remaining = len;

        while (remaining)
        {
            while (_transInFlight[_dmaNext])
            {
                if (!waitQueued())
                {
                    if (ownBus)
                        releaseBus();
                    return false;
                }
            }

            const int slot = _dmaNext;
            _dmaNext ^= 1;
            const size_t n = std::min(remaining, DmaChunkBytes);
            std::memcpy(_dmaBuf[slot], p, n);

            spi_transaction_t *t = _trans[slot];
            std::memset(t, 0, sizeof(*t));
            t->flags = (remaining > n) ? SPI_TRANS_CS_KEEP_ACTIVE : 0;
            t->length = static_cast<int>(n * 8U);
            t->tx_buffer = _dmaBuf[slot];

            const esp_err_t err = spi_device_queue_trans(_dev, t, portMAX_DELAY);
            if (err != ESP_OK)
            {
                _transInFlight[slot] = false;
                (void)flushQueued();
                if (ownBus)
                    releaseBus();
                return fail(display::IoError::QueueTx);
            }

            _transInFlight[slot] = true;
            ++_dmaInflight;
            p += n;
            remaining -= n;
        }
        return true;
    }

    uint8_t *LcdTransport::directPixelsBuffer(size_t &capacity)
    {
        capacity = 0;
        if (_mode != Mode::DmaStream || !_useDma || !_dev || !_dmaBuf[0] || !_dmaBuf[1] || !_trans[0] || !_trans[1])
            return nullptr;

        while (_transInFlight[_dmaNext])
        {
            if (!waitQueued())
                return nullptr;
        }

        capacity = DmaChunkBytes;
        return _dmaBuf[_dmaNext];
    }

    bool LcdTransport::submitDirectPixels(size_t len) noexcept
    {
        if (!len || len > DmaChunkBytes || !_useDma || !_dev)
            return fail(display::IoError::NotReady);
        setCsCached(0);
        setDcCached(1);
        if (!acquireBus())
            return false;

        while (_transInFlight[_dmaNext])
        {
            if (!waitQueued())
                return false;
        }

        const int slot = _dmaNext;
        _dmaNext ^= 1;

        spi_transaction_t *t = _trans[slot];
        std::memset(t, 0, sizeof(*t));
        t->flags = SPI_TRANS_CS_KEEP_ACTIVE;
        t->length = static_cast<int>(len * 8U);
        t->tx_buffer = _dmaBuf[slot];

        const esp_err_t err = spi_device_queue_trans(_dev, t, portMAX_DELAY);
        if (err != ESP_OK)
        {
            _transInFlight[slot] = false;
            return fail(display::IoError::QueueTx);
        }

        _transInFlight[slot] = true;
        ++_dmaInflight;
        return true;
    }

    bool IRAM_ATTR LcdTransport::fillPixels(uint16_t color, size_t count) noexcept
    {
        if (_mode != Mode::DriverCs)
            return display::Transport::fillPixels(color, count);

        if (!_dev || !_stage) [[unlikely]]
            return fail(display::IoError::NotReady);

        if (spi_device_acquire_bus(_dev, portMAX_DELAY) != ESP_OK) [[unlikely]]
            return fail(display::IoError::QueueTx);
        _busAcquired = true;

        constexpr size_t stagePixels = (CsStageBytes * 2) / sizeof(uint16_t);
        const uint8_t color_high = static_cast<uint8_t>(color >> 8);
        const uint8_t color_low = static_cast<uint8_t>(color & 0xFF);
        if (color_high == color_low)
        {
            std::memset(_stage, color_low, CsStageBytes * 2);
        }
        else
        {
            const uint32_t color32 = (static_cast<uint32_t>(color) << 16) | color;
            fastFill32(reinterpret_cast<uint32_t *>(_stage), stagePixels >> 1, color32);
        }

        size_t remaining = count;
        while (remaining)
        {
            while (_asyncInFlight >= MaxAsyncTrans)
            {
                if (!popOldest()) [[unlikely]]
                {
                    spi_device_release_bus(_dev);
                    _busAcquired = false;
                    return false;
                }
            }

            const size_t n = std::min(remaining, stagePixels);
            if (!queueAsync(_stage, n * sizeof(uint16_t), (remaining > n) ? SPI_TRANS_CS_KEEP_ACTIVE : 0U, 0U))
            {
                (void)drainQueue();
                spi_device_release_bus(_dev);
                _busAcquired = false;
                return false;
            }
            remaining -= n;
        }

        (void)drainQueue();
        spi_device_release_bus(_dev);
        _busAcquired = false;
        return true;
    }

    bool LcdTransport::flush() noexcept
    {
        return waitComplete();
    }

    bool IRAM_ATTR LcdTransport::waitComplete() noexcept
    {
        const bool ok = drainQueue();
        releaseBus();
        return ok;
    }
}

#endif
