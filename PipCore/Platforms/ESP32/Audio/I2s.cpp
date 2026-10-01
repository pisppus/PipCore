#include "Config.hpp"
#if PIPCORE_TARGET_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s_std.h>
#include <esp_log.h>

#include "Platforms/ESP32/Audio/I2s.hpp"

namespace pipcore::esp32::services
{
    namespace
    {
        constexpr const char *kTag = "I2S";
    }

    I2sOut::~I2sOut()
    {
        deinit();
    }

    bool I2sOut::init(const audio::BackendConfig &cfg) noexcept
    {
        if (cfg.sampleRate == 0)
            return false;

        deinit();

        _sampleRate = cfg.sampleRate;
        _pump = cfg.pump;
        _pumpUser = cfg.pumpUser;

        ESP_LOGI(kTag, "port=%u rate=%u bck=%d ws=%d dout=%d", (unsigned)cfg.i2sPort, (unsigned)_sampleRate,
                 (int)cfg.bck, (int)cfg.ws, (int)cfg.dataOut);

        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(static_cast<int>(cfg.i2sPort), I2S_ROLE_MASTER);
        chan_cfg.dma_desc_num = 4;
        chan_cfg.dma_frame_num = 64;
        chan_cfg.auto_clear = true;

        i2s_chan_handle_t tx_chan = nullptr;
        esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, nullptr);
        if (err != ESP_OK || !tx_chan)
        {
            ESP_LOGE(kTag, "i2s_new_channel failed: %d", static_cast<int>(err));
            return false;
        }

        i2s_std_config_t std_cfg = {};
        std_cfg.clk_cfg.sample_rate_hz = _sampleRate;
        std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
        std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

        std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
        std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
        std_cfg.gpio_cfg.bclk = static_cast<gpio_num_t>(cfg.bck);
        std_cfg.gpio_cfg.ws = static_cast<gpio_num_t>(cfg.ws);
        std_cfg.gpio_cfg.dout = static_cast<gpio_num_t>(cfg.dataOut);
        std_cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
        std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
        std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
        std_cfg.gpio_cfg.invert_flags.ws_inv = false;

        err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(kTag, "i2s_channel_init_std_mode failed: %d", static_cast<int>(err));
            i2s_del_channel(tx_chan);
            return false;
        }

        err = i2s_channel_enable(tx_chan);
        if (err != ESP_OK)
        {
            ESP_LOGE(kTag, "i2s_channel_enable failed: %d", static_cast<int>(err));
            i2s_del_channel(tx_chan);
            return false;
        }

        _txChan = tx_chan;

        if (_pump)
        {
            _taskExited.store(false);
            _run.store(true);
            if (xTaskCreatePinnedToCore(
                    mixerTaskEntry, "PipCoreAudio",
                    static_cast<configSTACK_DEPTH_TYPE>(CONFIG_PIPCORE_AUDIO_TASK_STACK), this,
                    static_cast<UBaseType_t>(CONFIG_PIPCORE_AUDIO_TASK_PRIO), &_taskHandle,
                    (CONFIG_PIPCORE_AUDIO_TASK_CORE < (int)configNUM_CORES)
                        ? static_cast<BaseType_t>(CONFIG_PIPCORE_AUDIO_TASK_CORE)
                        : tskNO_AFFINITY) == pdPASS)
            {
                _ready.store(true, std::memory_order_relaxed);
                return true;
            }

            ESP_LOGE(kTag, "mixer task create failed");
            _run.store(false);
            _taskHandle = nullptr;
            i2s_channel_disable(tx_chan);
            i2s_del_channel(tx_chan);
            _txChan = nullptr;
            return false;
        }

        _ready.store(true, std::memory_order_relaxed);
        return true;
    }

    void I2sOut::mixerTaskEntry(void *arg) noexcept
    {
        static_cast<I2sOut *>(arg)->mixerLoop();

        static_cast<I2sOut *>(arg)->_taskExited.store(true, std::memory_order_release);
        vTaskDelete(nullptr);
    }

    void I2sOut::mixerLoop() noexcept
    {
        while (_run.load(std::memory_order_relaxed))
        {
            if (!_txChan || !_ready.load(std::memory_order_relaxed))
            {

                vTaskDelay(pdMS_TO_TICKS(2));
                continue;
            }
            _pump(_blockBuf, 256, _pumpUser);
            writeInterleavedS16(_blockBuf, 256);
        }
    }

    void I2sOut::deinit() noexcept
    {
        _ready.store(false, std::memory_order_relaxed);

        if (_taskHandle)
        {
            _run.store(false, std::memory_order_relaxed);

            int guard = 0;
            while (!_taskExited.load(std::memory_order_acquire) && guard < 500)
            {
                vTaskDelay(pdMS_TO_TICKS(2));
                ++guard;
            }
            _taskHandle = nullptr;

            if (!_taskExited.load(std::memory_order_acquire))
            {

                ESP_LOGE(kTag, "mixer task did not exit; I2S channel left alive");
                return;
            }
        }

        if (_txChan)
        {
            i2s_channel_disable(_txChan);
            i2s_del_channel(_txChan);
            _txChan = nullptr;
        }
    }

    void I2sOut::writeInterleavedS16(const int16_t *interleavedLR, size_t frames) noexcept
    {
        if (!_txChan || !interleavedLR || frames == 0)
            return;

        const uint8_t *p = reinterpret_cast<const uint8_t *>(interleavedLR);
        size_t remaining = frames * 2 * sizeof(int16_t);
        while (remaining > 0)
        {
            if (!_run.load(std::memory_order_relaxed) || !_ready.load(std::memory_order_relaxed))
                return;

            size_t written = 0;
            const esp_err_t err = i2s_channel_write(_txChan, p, remaining, &written, pdMS_TO_TICKS(100));
            if (err != ESP_OK && err != ESP_ERR_TIMEOUT)
                return;
            p += written;
            remaining -= written;
        }
    }
}

#endif
