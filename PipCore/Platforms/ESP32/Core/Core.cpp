#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_cpu.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "Platforms/ESP32/Core/Core.hpp"
#include "Platforms/ESP32/Core/Alloc.hpp"
#if PIPCORE_ENABLE_PREFS || PIPCORE_ENABLE_WIFI
#include <nvs_flash.h>
#endif
#include <Debug.hpp>
#include <Log.hpp>

uint64_t pipcore::debug::profileCycles() noexcept
{
    return esp_cpu_get_cycle_count();
}

#if PIPCORE_ENABLE_DEBUG
pipcore::debug::AllocStats pipcore::debug::allocStats() noexcept
{
    AllocStats stats;
    Tracker &tracker = Tracker::instance();
    tracker.lock();
    stats.currentBytes = tracker._totalAllocated.load(std::memory_order_relaxed);
    stats.peakBytes = tracker._peakAllocated;
    tracker.unlock();
    return stats;
}
#endif

namespace pipcore::debug::detail
{
    namespace
    {

        portMUX_TYPE &profilerMux() noexcept
        {
            static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
            return mux;
        }
    }

    void profilerLock() noexcept
    {
        taskENTER_CRITICAL(&profilerMux());
    }

    void profilerUnlock() noexcept
    {
        taskEXIT_CRITICAL(&profilerMux());
    }
}

namespace pipcore::esp32
{
#if PIPCORE_ENABLE_PREFS || PIPCORE_ENABLE_WIFI
    bool nvsEnsureReady() noexcept
    {
        static bool ready = false;
        if (ready)
            return true;

        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {

            if (nvs_flash_erase() != ESP_OK)
            {
                log::error("nvs: flash erase failed (err=%d)", static_cast<int>(err));
                return false;
            }
            err = nvs_flash_init();
        }
        ready = (err == ESP_OK);
        if (!ready)
            log::error("nvs: init failed (err=%d)", static_cast<int>(err));
        return ready;
    }
#endif
}

namespace pipcore::esp32::services
{
    void Gpio::pinModeInput(uint8_t pin, InputMode mode) const noexcept
    {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_INPUT;
        io_conf.pin_bit_mask = (1ULL << pin);
        io_conf.pull_down_en = (mode == InputMode::Pulldown) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = (mode == InputMode::Pullup) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
        gpio_config(&io_conf);
    }

    bool Gpio::digitalRead(uint8_t pin) const noexcept
    {
        return gpio_get_level(static_cast<gpio_num_t>(pin)) != 0;
    }

    int16_t Gpio::analogRead(uint8_t pin) const noexcept
    {
        adc_unit_t unit;
        adc_channel_t chan;

        if (adc_oneshot_io_to_channel(static_cast<int>(pin), &unit, &chan) != ESP_OK)
        {
            return 0;
        }

        static adc_oneshot_unit_handle_t s_adc1 = nullptr;
        static adc_oneshot_unit_handle_t s_adc2 = nullptr;
        static uint32_t s_chan_mask1 = 0;
        static uint32_t s_chan_mask2 = 0;

        adc_oneshot_unit_handle_t &handle = (unit == ADC_UNIT_1) ? s_adc1 : s_adc2;
        uint32_t &chan_mask = (unit == ADC_UNIT_1) ? s_chan_mask1 : s_chan_mask2;

        if (!handle)
        {
            adc_oneshot_unit_init_cfg_t init_cfg = {};
            init_cfg.unit_id = unit;
            init_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;
            if (adc_oneshot_new_unit(&init_cfg, &handle) != ESP_OK)
            {
                return 0;
            }
        }

        const uint32_t bit = (1U << static_cast<uint32_t>(chan));
        if ((chan_mask & bit) == 0)
        {
            adc_oneshot_chan_cfg_t chan_cfg = {};
            chan_cfg.atten = ADC_ATTEN_DB_12;
            chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

            if (adc_oneshot_config_channel(handle, chan, &chan_cfg) != ESP_OK)
            {
                return 0;
            }
            chan_mask |= bit;
        }

        int raw = 0;
        if (adc_oneshot_read(handle, chan, &raw) == ESP_OK)
        {
            return static_cast<int16_t>(raw);
        }

        return 0;
    }

    void *Heap::alloc(size_t bytes, AllocCaps caps) const noexcept
    {
        if (bytes == 0)
            return nullptr;
        if (caps == AllocCaps::PreferInternal)
        {
            void *p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
            return p ? p : heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        }
        return heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
    }

    void Heap::free(void *ptr) const noexcept
    {
        heap_caps_free(ptr);
    }

    void *Heap::allocAligned(size_t bytes, size_t align, AllocCaps caps) const noexcept
    {
        if (bytes == 0)
            return nullptr;
        if (align < sizeof(void *))
            align = sizeof(void *);
        if (caps == AllocCaps::PreferInternal)
        {
            void *p = heap_caps_aligned_alloc(align, bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
            return p ? p : heap_caps_aligned_alloc(align, bytes, MALLOC_CAP_8BIT);
        }
        return heap_caps_aligned_alloc(align, bytes, MALLOC_CAP_8BIT);
    }

    void Heap::freeAligned(void *ptr) const noexcept
    {
        heap_caps_free(ptr);
    }

    uint32_t Heap::freeHeapTotal() const noexcept
    {
        return esp_get_free_heap_size();
    }
    uint32_t Heap::freeHeapInternal() const noexcept
    {
        return heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    }
    uint32_t Heap::largestFreeBlock() const noexcept
    {
        return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    }
    uint32_t Heap::minFreeHeap() const noexcept
    {
        return esp_get_minimum_free_heap_size();
    }

    uint32_t Time::nowMs() const noexcept
    {
        return pipcore::esp32::nowMs();
    }

    uint64_t Time::nowUs() const noexcept
    {
        return static_cast<uint64_t>(esp_timer_get_time());
    }
}
